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
#include "Fonts.h"
#include "screens/Screen.h"
#include "screens/RunScreen.h"
#include "screens/MenuScreen.h"
#include "screens/EditValueScreen.h"

// The library header (included above) already defines GC9A01A_SPICLOCK as
// 30 MHz, so a plain #ifndef override never fires — the panel had been blitting
// at 30 MHz (~30 ms/frame, capping the frame rate at ~32 fps).  #undef first so
// the intended faster rate actually reaches tft.begin().
#undef GC9A01A_SPICLOCK
#define GC9A01A_SPICLOCK       48000000
#define GC9A01A_SPICLOCK_READ  2000000

// Framebuffers: all drawing targets RAM, never the panel directly.
//
// With DISPLAY_DOUBLE_BUFFER the two are used front/back: the panel is fed from
// the front buffer by DMA while the next frame is drawn into the back one, then
// they swap.  The transfer is ~19 ms at 48 MHz (240*240*16 bits) and used to be
// dead time — the CPU blocked in updateScreen() for it.  Now the frame costs
// max(draw, blit) instead of draw + blit, and input and acquisition keep being
// serviced throughout.
// 32-byte aligned because updateScreenAsync() flushes the dcache over the whole
// buffer before handing it to DMA, and cache maintenance works in lines.
DMAMEM __attribute__((aligned(32))) uint16_t fb1[240 * 240];
#if DISPLAY_DOUBLE_BUFFER
DMAMEM __attribute__((aligned(32))) uint16_t fb2[240 * 240];
uint16_t* frontBuffer = fb1;   // being displayed / blitted
uint16_t* backBuffer  = fb2;   // being drawn into
bool      framePending = false;  // back buffer holds a frame not yet handed over
#endif

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
    // DMAMEM is not zero-initialised, and persistence reads the previous frame
    // back, so start both buffers black rather than from whatever was in OCRAM.
    memset(fb1, 0, sizeof fb1);
#if DISPLAY_DOUBLE_BUFFER
    memset(fb2, 0, sizeof fb2);
#endif
    tft.setFrameBuffer(fb1);
    tft.useFrameBuffer(true);

    input.begin();

    settings.load();   // stored settings, or defaults on first boot
    state.load();      // stored acquisition setup, or defaults on first boot

    // Wire the screen graph: RunScreen (root) → MenuScreen → EditValueScreen.
    menuScreen.setEditScreen(&editScreen);
    runScreen.setMenuScreen(&menuScreen);

    // Push the run screen as the root; the menu is pushed on demand.
    screens.reset(&runScreen, ctx);

    lastFpsTime = millis();
}

#if ACQ_DIAG
// Slowest draw each second.  With double buffering this is draw time only —
// the transfer overlaps the next frame's work, so it no longer caps fps.
static void reportDrawTime(uint32_t drawUs) {
    static uint32_t drawMaxUs = 0, drawRepMs = 0;
    if (drawUs > drawMaxUs) drawMaxUs = drawUs;
    if (millis() - drawRepMs >= 1000) {
        Serial.printf("draw: max=%luus\n", (unsigned long)drawMaxUs);
        drawMaxUs = 0;
        drawRepMs = millis();
    }
}
#endif

#if UI_DEBUG_GRID
// Layout ruler: 16 px lines anchored on the display centre (so the centre axes
// land on a line), drawn over everything.  The centre axes are brighter so the
// eye can count divisions outward from them.
static void drawDebugGrid(Renderer& r) {
    for (int16_t d = 0; d <= Theme::CX; d += 16) {
        const uint16_t c = (d == 0) ? Theme::Dim : Theme::DimDark;
        r.vline((int16_t)(Theme::CX - d), 0, Theme::H, c);
        r.vline((int16_t)(Theme::CX + d), 0, Theme::H, c);
        r.hline(0, (int16_t)(Theme::CY - d), Theme::W, c);
        r.hline(0, (int16_t)(Theme::CY + d), Theme::W, c);
    }
}
#endif

// Redraw only when something changed: a handled input event (UI dirty) or a
// newly completed acquisition frame.  Between those the loop just polls input
// and nudges acquisition, so iterations are microseconds long and input stays
// responsive even while a slow sweep fills.  Start true to force the first draw.
bool uiDirty = true;

void loop() {
    // 1. Drain all pending input events and forward each to the top screen.
    InputEvent e;
    while (input.poll(e)) {
        screens.handleEvent(e, ctx);
        uiDirty = true;
    }

    // 2. Advance the top screen's time-based work (non-blocking acquisition).
    const bool newFrame = screens.tick(ctx);

    // 3. Redraw only when there is something new to show — gating this is what
    //    keeps the UI responsive at long timebases.
#if DISPLAY_DOUBLE_BUFFER
    if (!framePending && (uiDirty || newFrame)) {
#if ACQ_DIAG
        const uint32_t drawStart = micros();
#endif
        // Draw into the back buffer.  The front one may still be mid-transfer;
        // it is a different block of memory and the running DMA reads its own
        // descriptors, so retargeting the driver here is safe.
        tft.setFrameBuffer(backBuffer);
        renderer.setPreviousFrame(frontBuffer);
        screens.draw(renderer, ctx);

#if UI_DEBUG_GRID
        drawDebugGrid(renderer);
#endif
#if ACQ_DIAG
        reportDrawTime(micros() - drawStart);
#endif
        framePending = true;
        uiDirty      = false;
    }

    // 4. Hand the finished frame over once the previous transfer has drained.
    //    Deliberately a poll rather than waitUpdateAsyncComplete(): until the
    //    panel is ready the loop falls through and keeps draining input and
    //    nudging acquisition instead of spinning.
    //    Only swap once the transfer has actually been accepted; if it were
    //    refused the frame would otherwise be dropped and the buffers rotated
    //    out from under it.
    if (framePending && !tft.asyncUpdateActive() && tft.updateScreenAsync()) {
        uint16_t* done = frontBuffer;
        frontBuffer = backBuffer;   // now on its way to the panel
        backBuffer  = done;         // free to draw into next time round
        framePending = false;
        countFrame();
    }
#else
    if (uiDirty || newFrame) {
#if ACQ_DIAG
        const uint32_t drawStart = micros();
#endif
        screens.draw(renderer, ctx);

#if UI_DEBUG_GRID
        drawDebugGrid(renderer);
#endif

        tft.updateScreen();

#if ACQ_DIAG
        reportDrawTime(micros() - drawStart);
#endif
        countFrame();
        uiDirty = false;
    }
#endif
}
