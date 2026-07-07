// FontTest.cpp — Throwaway interactive font auditioning sketch (font-tester branch).
//
// Renders candidate fonts on the actual GC9A01A panel so typefaces/sizes can be
// judged where it matters (anti-aliasing on the panel, through the round lens).
// Flash once and drive it with the hardware — no reflash per font:
//
//   Encoder turn : cycle through the font list (typeface + size)
//   B1 (Mode)    : cycle the sample string
//   B2 (Channel) : toggle single-sample view ↔ UI-mock view (several HUD strings)
//
// Font systems shown side by side:
//   - Default GFX 5x7 (what the app uses today), scaled x1/x2
//   - t3 anti-aliased (Arial / ArialBold) — the quality upgrade
//   - Adafruit GFX 1-bit (FreeSans / FreeSerif / FreeMono) — typeface variety
//
// This file is compiled ALONE (see platformio.ini build_src_filter on this
// branch); the real app is not built here.  Delete the branch when done.

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <GC9A01A_t3n.h>
#include <Encoder.h>

#include "Config.h"

// t3 anti-aliased fonts (bundled with the Teensy ILI9341_t3 library).
// The font files #include "ILI9341_t3.h" only to get the ILI9341_t3_font_t
// struct — but GC9A01A_t3n already defines it, so pre-define that header's guard
// to skip it and avoid a duplicate struct definition.
#define _ILI9341_t3H_
#include "font_Arial.h"
#include "font_ArialBold.h"

// Adafruit GFX 1-bit fonts (typeface variety).
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

#ifndef GC9A01A_SPICLOCK
#  define GC9A01A_SPICLOCK      96000000
#endif
#define GC9A01A_SPICLOCK_READ  2000000

// Colors (RGB565).  Prefixed to avoid the library's BLACK/WHITE/... macros.
static constexpr uint16_t kBlack = 0x0000, kWhite = 0xFFFF, kDim = 0xC618;

DMAMEM uint16_t fb1[240 * 240];
GC9A01A_t3n tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

// --- Font list --------------------------------------------------------------
enum Kind : uint8_t { DEF, T3, GFX };
struct Entry {
    const char*              name;
    Kind                     kind;
    const ILI9341_t3_font_t* t3;    // for T3
    const GFXfont*           gfx;   // for GFX
    uint8_t                  size;  // setTextSize for DEF/GFX
};

static const Entry kFonts[] = {
    { "Default x1",     DEF, nullptr,      nullptr,             1 },
    { "Default x2",     DEF, nullptr,      nullptr,             2 },
    { "Arial 11",       T3,  &Arial_11,    nullptr,             1 },
    { "Arial 13",       T3,  &Arial_13,    nullptr,             1 },
    { "Arial 16",       T3,  &Arial_16,    nullptr,             1 },
    { "Arial 20",       T3,  &Arial_20,    nullptr,             1 },
    { "Arial 24",       T3,  &Arial_24,    nullptr,             1 },
    { "Arial 14 Bold",  T3,  &Arial_14_Bold, nullptr,           1 },
    { "Arial 18 Bold",  T3,  &Arial_18_Bold, nullptr,           1 },
    { "FreeSans 9",     GFX, nullptr,      &FreeSans9pt7b,      1 },
    { "FreeSans 12",    GFX, nullptr,      &FreeSans12pt7b,     1 },
    { "FreeSansBold 9", GFX, nullptr,      &FreeSansBold9pt7b,  1 },
    { "FreeSerif 9",    GFX, nullptr,      &FreeSerif9pt7b,     1 },
    { "FreeSerif 12",   GFX, nullptr,      &FreeSerif12pt7b,    1 },
    { "FreeMono 9",     GFX, nullptr,      &FreeMono9pt7b,      1 },
    { "FreeMonoBold 9", GFX, nullptr,      &FreeMonoBold9pt7b,  1 },
};
static constexpr int kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);

// Sample strings — real UI text so fonts are judged in context.
static const char* kSamples[] = {
    "500 us/div", "1.5 V/div", "Chan: A+B", "TRIG", "0123456789", "AaBbCcGgQq",
};
static constexpr int kSampleCount = sizeof(kSamples) / sizeof(kSamples[0]);

// --- State ------------------------------------------------------------------
static Encoder s_enc(ENC_B, ENC_A);   // B,A order matches the app
static long    s_lastDetent = 0;
static int     s_font = 4;             // start on Arial 16
static int     s_sample = 0;
static bool    s_mock = false;
static bool    s_dirty = true;

// Simple press-edge detect with a lockout (good enough for a tester).
struct Btn { uint8_t pin; bool last; uint32_t lockUntil; };
static Btn s_b1{ BTN_MODE, false, 0 };
static Btn s_b2{ BTN_CHAN, false, 0 };

static bool pressed(Btn& b) {
    uint32_t now = millis();
    bool down = (digitalRead(b.pin) == LOW);
    bool fired = down && !b.last && (int32_t)(now - b.lockUntil) >= 0;
    if (fired) b.lockUntil = now + 150;
    b.last = down;
    return fired;
}

static void applyFont(const Entry& e) {
    switch (e.kind) {
        case DEF: tft.setFont();       tft.setTextSize(e.size); break;
        case T3:  tft.setFont(*e.t3);  tft.setTextSize(1);      break;
        case GFX: tft.setFont(e.gfx);  tft.setTextSize(e.size); break;
    }
}

void setup() {
    Serial.begin(115200);
    SPI.begin();
    tft.begin(GC9A01A_SPICLOCK, GC9A01A_SPICLOCK_READ);
    tft.setRotation(2);
    tft.setFrameBuffer(fb1);
    tft.useFrameBuffer(true);

    pinMode(BTN_MODE, INPUT_PULLUP);
    pinMode(BTN_CHAN, INPUT_PULLUP);
    s_lastDetent = s_enc.read() / 4;
}

static void draw() {
    const Entry& e = kFonts[s_font];
    tft.fillScreen(kBlack);

    // Header (always in the default font so the label stays readable).
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(kDim);
    char hdr[40];
    snprintf(hdr, sizeof hdr, "%d/%d  %s", s_font + 1, kFontCount, e.name);
    tft.setCursor(46, 28);
    tft.print(hdr);

    // Body in the selected font.
    applyFont(e);
    tft.setTextColor(kWhite);
    if (s_mock) {
        // A mock HUD: several real strings stacked in this font.
        const char* rows[] = { "TRIG", "500 us/div", "Chan: A+B", "1.5 V/div" };
        int16_t y = 60;
        for (uint8_t i = 0; i < 4; ++i) {
            tft.setCursor(40, y);
            tft.print(rows[i]);
            y += 38;
        }
    } else {
        tft.setCursor(30, 118);
        tft.print(kSamples[s_sample]);
    }

    // Footer hint.
    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(kDim);
    tft.setCursor(44, 210);
    tft.print("ENC font  B1 text  B2 mock");

    tft.updateScreen();
}

void loop() {
    long detent = s_enc.read() / 4;
    if (detent != s_lastDetent) {
        int delta = (int)(detent - s_lastDetent);
        s_lastDetent = detent;
        s_font = (s_font + delta) % kFontCount;
        if (s_font < 0) s_font += kFontCount;
        s_dirty = true;
    }
    if (pressed(s_b1)) { s_sample = (s_sample + 1) % kSampleCount; s_dirty = true; }
    if (pressed(s_b2)) { s_mock = !s_mock; s_dirty = true; }

    if (s_dirty) {
        draw();
        s_dirty = false;
    }
}
