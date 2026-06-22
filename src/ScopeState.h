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
    uint16_t vscale_mv_per_div[2]   = {1000, 1000}; // mV/div per channel
    int16_t  trigger_level_mv       = 0;           // mV
    bool     channelEnabled[2]      = {true, true};

    // Restore all fields to the compile-time defaults above.
    void resetToDefaults();
};

// Human-readable names for UI display.
const char* modeName(Mode m);
const char* channelName(ChannelSel c);
