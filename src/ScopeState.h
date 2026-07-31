// ScopeState.h — Oscilloscope runtime state shared across all screens.
//
// ScopeState is the single authoritative record for what the scope is doing:
// which mode, which channel, timebase, voltage scale, trigger level, and
// run/stop status.  All screens read from and write to one shared instance
// via AppContext.  Real acquisition (Task 4) populates the waveform buffers.
#pragma once

#include <stdint.h>

// Acquisition / display mode.
enum class Mode : uint8_t { Triggered, Rolling, XY, Spectrum, COUNT };

// Spectrum mode samples at a fixed rate rather than a user timebase: with the
// interval = timebase * GridCols / N mapping (8/240, integer), 938 µs/div gives
// a 31 µs interval → ~32 kHz sample rate → ~16 kHz Nyquist.  A 256-point FFT
// then yields ~126 Hz per bin across 128 buckets, spanning ~126 Hz to ~16 kHz
// (sub-bin content, e.g. 40 Hz, folds into the lowest bucket) — the requested
// ~40 Hz–16 kHz audio range.  Stored in Spectrum's per-mode timebase slot and
// never exposed to the encoder (see paramAppliesInMode).
static constexpr uint32_t kSpectrumTimebaseUs = 938;

// Which analog channel(s) are selected.
enum class ChannelSel : uint8_t { A, B, Both };

// Which parameter the encoder currently controls.
enum class EncoderParam : uint8_t { Timebase, VScale, TriggerLevel };

struct ScopeState {
    Mode         mode     = Mode::Triggered;
    ChannelSel   channel  = ChannelSel::Both;   // A+B (channel select hidden in v2 UI)
    EncoderParam selected = EncoderParam::Timebase;
    bool         running  = true;

    // Timebase is per-mode: the three modes now cover different ranges
    // (Triggered 50 µs–10 ms, Rolling 500 µs–1 s, XY 500 µs–100 ms), so each
    // remembers its own setting and switching modes restores it.  Indexed by
    // (uint8_t)Mode; use timebase()/setTimebase() for the active mode.  uint32
    // is wide enough for 1 s/div (1,000,000 µs overflows uint16_t).
    uint32_t timebase_us_per_div[(uint8_t)Mode::COUNT] =
        {500, 500, 500, kSpectrumTimebaseUs};
    uint16_t vscale_mv_per_div[2]   = {3000, 3000};  // mV/div per channel (3 V/div default)
    int16_t  trigger_level_mv       = 0;           // mV
    bool     channelEnabled[2]      = {true, true};

    // Single-shot: when true, the next successful triggered capture freezes the
    // display (running → false) and disarms.  Set by B3 long-press.
    bool     singleArmed            = false;

    // Active timebase = the current mode's stored setting.
    uint32_t timebase() const { return timebase_us_per_div[(uint8_t)mode]; }
    void setTimebase(uint32_t us) { timebase_us_per_div[(uint8_t)mode] = us; }

    // Restore all fields to the compile-time defaults above.
    void resetToDefaults();

    // Persistence (Teensy emulated EEPROM), stored separately from Settings.
    // Persists the acquisition setup (mode, channel, selected param, timebase,
    // V/div, trigger level, channel enables).  The transient run/stop and
    // single-shot flags are NOT persisted — the scope always boots running and
    // disarmed.  load() applies a stored record if present, else sets defaults
    // and writes them.  save() persists the current setup (call it debounced,
    // not on every encoder detent).
    void load();
    void save() const;
};

// Human-readable names for UI display.
const char* modeName(Mode m);
const char* channelName(ChannelSel c);
