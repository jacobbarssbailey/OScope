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

// ---- Capture diagnostics ----
// Set to 1 to emit the 1 Hz `acq:` line and the per-cell `cell:` aggregate on
// Serial, which is what docs/acq-characterization.md's bench protocol reads.
// Off in shipping builds: it is a characterization tool, not a user feature, so
// it is compile-time rather than a settings toggle (nothing to leave switched on
// by accident, and the reporting code drops out entirely).
#define ACQ_DIAG 0

// ---- Display transfer ----
// 1 = hand each frame to the panel asynchronously and let the main loop carry
// on: the ~19 ms DMA transfer no longer blocks, so input and acquisition keep
// being serviced through it instead of stalling.  The next frame is still drawn
// only once the transfer has drained — there is one framebuffer, so drawing
// during it would tear.
//
// NOT double buffering, deliberately.  Swapping buffers means calling
// setFrameBuffer() per frame, which clears the driver's DMA-init flag and makes
// it re-run initDMASettings() -> DMAChannel::begin(true).  On Teensy 4 that call
// allocates a new DMA channel WITHOUT releasing the old one, so it leaks one
// channel per frame; the 16-channel pool empties in about eleven frames and the
// next begin() returns a null TCD and hardfaults.  Measured on hardware:
// dma_channel_allocated_mask climbing 001F -> 03FF -> 7FFF over 300 ms, then a
// reset.  Overlapping draw with the transfer was worth ~4% here anyway (the
// draw is far shorter than the blit), so this keeps the whole win and none of
// the bug.  See docs/display-async.md.
//
// 0 = the original blocking updateScreen().
#define DISPLAY_ASYNC_BLIT 1

// ---- UI layout debugging ----
// Set to 1 to overlay a 16 px ruler grid (anchored on the display centre, with
// the centre axes picked out brighter) on top of every screen.  It is the
// measuring stick for tuning margins and text positions against the design
// mockups; always 0 in shipping builds.
#define UI_DEBUG_GRID 0
