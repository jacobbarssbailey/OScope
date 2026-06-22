/*
 * OScope — Task 1 verification harness
 *
 * Displays the most-recently received InputEvent on the round 240×240
 * GC9A01A display. This is the temporary verification UI for milestone 1;
 * it will be replaced by the real oscilloscope UI in later milestones.
 *
 * Hardware: Teensy 4.0.  All pin assignments are in Config.h.
 */

#include <Adafruit_GFX.h>
#include <GC9A01A_t3n.h>
#include <SPI.h>

#include "Config.h"
#include "Input.h"

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

// Input module and last-event string for the verification display.
Input input;
char  lastEvent[32] = "none";

void setup() {
    Serial.begin(115200);

    SPI.begin();

    tft.begin(GC9A01A_SPICLOCK, GC9A01A_SPICLOCK_READ);
    tft.setRotation(2);
    tft.setFrameBuffer(fb1);
    tft.useFrameBuffer(true);

    input.begin();

    lastFpsTime = millis();
}

void loop() {
    // Drain all pending input events; keep only the last description.
    InputEvent e;
    while (input.poll(e)) {
        if (e.type == EventType::EncoderTurn) {
            snprintf(lastEvent, sizeof lastEvent, "enc %+d", e.delta);
        } else if (e.type == EventType::ShortPress) {
            snprintf(lastEvent, sizeof lastEvent, "short b%d", (int)e.button);
        } else if (e.type == EventType::LongPress) {
            snprintf(lastEvent, sizeof lastEvent, "LONG  b%d", (int)e.button);
        }
    }

    // Render: white text centred on the round display.
    tft.fillScreen(0);
    tft.setTextSize(2);
    tft.setCursor(40, 110);
    tft.setTextColor(0xFFFF);
    tft.print(lastEvent);
    tft.updateScreen();

    countFrame();
}
