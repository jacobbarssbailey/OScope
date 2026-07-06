/*
 * OScope — Dual-channel Eurorack oscilloscope (Teensy 4.0)
 *
 * This is the main Arduino sketch.  It owns:
 *   - The framebuffer and display driver (GC9A01A_t3n, 240×240 round panel)
 *   - The FPS counter (countFrame / fps)
 *   - The top-level event/render loop
 *
 * It delegates all input decoding to Input, all drawing to Renderer, and all
 * UI logic to the ScreenStack.  The temporary Task-1 verification display has
 * been replaced by the screen-stack framework introduced in Task 2.
 *
 * Hardware: Teensy 4.0.  All pin assignments are in Config.h.
 */

#include <Adafruit_GFX.h>
#include <GC9A01A_t3n.h>
#include <SPI.h>

#include "Config.h"
#include "Input.h"
#include "Renderer.h"
#include "ScopeState.h"
#include "Settings.h"
#include "Theme.h"
#include "screens/Screen.h"
#include "screens/RunScreen.h"
#include "screens/MenuScreen.h"
#include "screens/EditValueScreen.h"

// SPI clock for the panel.  96 MHz works for a CPU-driven (sync) blit, but with
// the async (DMA) blit the display DMA competes with the ADC DMA for RAM2
// bandwidth; a slower clock frees bandwidth for acquisition.  40 MHz here is an
// experiment to test whether the signal glitching is display/ADC DMA contention
// (tune back up toward 96 MHz once we know where it goes clean).
#ifndef GC9A01A_SPICLOCK
#  define GC9A01A_SPICLOCK      40000000
#endif
#define GC9A01A_SPICLOCK_READ  2000000

// Framebuffer: all drawing targets this RAM buffer; tft.updateScreen() blits
// it to the panel in one SPI burst.  Never draw directly to the panel.
DMAMEM uint16_t fb1[240 * 240];

GC9A01A_t3n tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

// FPS tracking (harmless to keep; useful for later milestones).
uint32_t frameCount  = 0;
uint32_t lastFpsTime = 0;
float    fps         = 0.0f;

void countFrame() {
    frameCount++;
    uint32_t now     = millis();
    uint32_t elapsed = now - lastFpsTime;
    if (elapsed >= 1000) {
        fps          = (frameCount * 1000.0f) / elapsed;
        frameCount   = 0;
        lastFpsTime  = now;
    }
}

// ---- Application objects (all statically allocated, no new/delete) ----
Input           input;
ScopeState      state;
Settings        settings;
Renderer        renderer(tft);
ScreenStack     screens;
RunScreen       runScreen;
MenuScreen      menuScreen;
EditValueScreen editScreen;
AppContext      ctx{state, screens, settings};

void setup() {
    Serial.begin(115200);

    SPI.begin();

    tft.begin(GC9A01A_SPICLOCK, GC9A01A_SPICLOCK_READ);
    tft.setRotation(2);
    tft.setFrameBuffer(fb1);
    tft.useFrameBuffer(true);

    input.begin();

    settings.load();   // stored settings, or defaults on first boot

    // Wire the screen graph: RunScreen (root) → MenuScreen → EditValueScreen.
    menuScreen.setEditScreen(&editScreen);
    runScreen.setMenuScreen(&menuScreen);

    // Push the run screen as the root; the menu is pushed on demand.
    screens.reset(&runScreen, ctx);

    lastFpsTime = millis();
}

// "Redraw pending" flag: set by a handled input event or a newly completed
// acquisition frame, cleared only when we actually redraw.  Because the display
// blit is now asynchronous (DMA), a frame that arrives while a blit is in flight
// must stay pending rather than be dropped — hence a persistent flag rather than
// redrawing purely on this iteration's signals.  Start true to force first draw.
bool redrawPending = true;

void loop() {
    // 1. Drain all pending input events and forward each to the top screen.
    InputEvent e;
    while (input.poll(e)) {
        screens.handleEvent(e, ctx);
        redrawPending = true;
    }

    // 2. Advance the top screen's time-based work (non-blocking acquisition).
    if (screens.tick(ctx)) redrawPending = true;

    // 3. Redraw only when something changed AND the previous async blit has
    //    finished — the DMA reads fb1 in the background, so drawing into it while
    //    a blit is active would tear/garbage the frame.  When a blit is in flight
    //    we simply skip drawing this iteration and keep polling input / sampling
    //    (the CPU is free during the blit); the pending flag holds until we draw.
    if (redrawPending && !tft.asyncUpdateActive()) {
        screens.draw(renderer, ctx);

        // FPS overlay (debug) — only over the run screen, so it doesn't collide
        // with the menu/edit bottom hints.  Reflects the redraw rate.
        if (screens.top() == &runScreen) {
            char fbuf[12];
            snprintf(fbuf, sizeof fbuf, "%d fps", (int)(fps + 0.5f));
            renderer.text(Theme::RunFpsX, Theme::RunFpsY, fbuf, Theme::Dim, 1);
        }

        tft.updateScreenAsync();   // DMA blit; returns immediately
        countFrame();
        redrawPending = false;
    }
}
