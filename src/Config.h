// Config.h — Single source of truth for all pin assignments and
// compile-time constants.  No other file should hard-code pin numbers.
// Matches README.md "Pin Mapping" (Arduino digital pin numbers).
#pragma once

// ---- Display (GC9A01A) ----
#define TFT_SCLK 13
#define TFT_MOSI 11
#define TFT_DC   10
#define TFT_CS    9
#define TFT_RST   8

// ---- Encoder ----
#define SW_ENC   21  // Encoder push switch
#define ENC_A    20  // Rotary encoder A
#define ENC_B    19  // Rotary encoder B

// ---- Buttons (active-low, INPUT_PULLUP) ----
#define BTN_MODE 18  // B1 — Mode
#define BTN_CHAN 15  // B2 — Channel select
#define BTN_RUN  14  // B3 — Run/Stop

// ---- Indicator LEDs (PWM-capable) ----
#define LED1 2
#define LED2 3
#define LED3 4

// ---- Analog signal inputs ----
// A/B swapped to match hardware wiring (Channel A is physically on A3).
#define SIGNAL_A A3  // Channel A (Digital 17)
#define SIGNAL_B A2  // Channel B (Digital 16)

// ---- Timing constants ----
#define LONG_PRESS_MS 500  // ms held before a LongPress event fires
