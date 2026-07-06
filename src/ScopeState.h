// ScopeState.h — Oscilloscope runtime state shared across all screens.
//
// ScopeState is the single authoritative record for what the scope is doing:
// which mode, which channel, timebase, voltage scale, trigger level, and
// run/stop status.  All screens read from and write to one shared instance
// via AppContext.  Real acquisition (Task 4) populates the waveform buffers.
#pragma once

#include <stdint.h>

// Acquisition / display mode.
enum class Mode : uint8_t { Triggered, Rolling, XY, COUNT };

// Which analog channel(s) are selected.
enum class ChannelSel : uint8_t { A, B, Both };

// Which parameter the encoder currently controls.
enum class EncoderParam : uint8_t { Timebase, VScale, TriggerLevel };

struct ScopeState {
    Mode         mode     = Mode::Triggered;
    ChannelSel   channel  = ChannelSel::A;
    EncoderParam selected = EncoderParam::Timebase;
    bool         running  = true;

    uint16_t timebase_us_per_div    = 500;        // µs/div
    uint16_t vscale_mv_per_div[2]   = {700, 700};  // mV/div per channel
    int16_t  trigger_level_mv       = 0;           // mV
    bool     channelEnabled[2]      = {true, true};

    // Single-shot: when true, the next successful triggered capture freezes the
    // display (running → false) and disarms.  Set by B3 long-press.
    bool     singleArmed            = false;

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
