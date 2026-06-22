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
#include "Theme.h"
#include "screens/Screen.h"
#include "screens/RunScreen.h"

// Note: GC9A01A_SPICLOCK is also defined in the library header (30 MHz);
// redefine here to use the faster 96 MHz rate the Teensy 4.0 can sustain.
#ifndef GC9A01A_SPICLOCK
#  define GC9A01A_SPICLOCK      96000000
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
Input      input;
ScopeState state;
Renderer   renderer(tft);
ScreenStack screens;
RunScreen   runScreen;
AppContext  ctx{state, screens};

void setup() {
    Serial.begin(115200);

    SPI.begin();

    tft.begin(GC9A01A_SPICLOCK, GC9A01A_SPICLOCK_READ);
    tft.setRotation(2);
    tft.setFrameBuffer(fb1);
    tft.useFrameBuffer(true);

    input.begin();

    // Push the run screen as the root; later milestones may push sub-screens.
    screens.reset(&runScreen, ctx);

    lastFpsTime = millis();
}

void loop() {
    // 1. Drain all pending input events and forward each to the top screen.
    InputEvent e;
    while (input.poll(e)) {
        screens.handleEvent(e, ctx);
    }

    // 2. Draw the current top screen into the framebuffer.
    screens.draw(renderer, ctx);

    // 3. Blit the framebuffer to the panel in one SPI burst.
    tft.updateScreen();

    countFrame();
}
