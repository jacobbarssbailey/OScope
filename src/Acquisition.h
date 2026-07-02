// Acquisition.h — ADC sampling engine for OScope.
//
// Acquisition owns the blocking single-sweep capture loop.  It reads both
// analog input channels (SIGNAL_A / SIGNAL_B from Config.h) into a
// SampleBuffers and handles trigger alignment for Triggered mode.
//
// Usage:
//   Acquisition acq;
//   acq.begin();          // call once in setup()
//   acq.capture(state, buf);  // call each frame when state.running
//
// Timing model (documented; see capture() in Acquisition.cpp for details):
//   timebase_us_per_div : µs per grid division (set by encoder)
//   GridDiv (= 30 px)   : pixels per grid division (from Theme.h)
//   SampleBuffers::N    : total samples = display width (240)
//   Samples per div     : SampleBuffers::N / GridCols  = 240/8 = 30 samples/div
//   Sample interval     : timebase_us_per_div / samples_per_div
//                       = timebase_us_per_div * GridCols / SampleBuffers::N
//                       = timebase_us_per_div * 8 / 240  [µs/sample]
//   At 500 µs/div       : interval = 500*8/240 ≈ 16.7 µs/sample
//   At 1 ms/div         : interval = 1000*8/240 ≈ 33.3 µs/sample
//   Interval is clamped to a minimum of 1 µs (analogRead takes ~1–2 µs on T4).
#pragma once

#include "modes/ScopeMode.h"
#include "ScopeState.h"

class Acquisition {
public:
    // Set ADC resolution to 10 bits (0..1023).  Call once in setup().
    void begin();

    // Capture one full sweep into buf.
    // For Triggered mode: pre-scans channel A for a rising edge crossing
    // trigger_level_mv; aligns sweep start to that crossing.  Falls through
    // to a free-run sweep if no crossing is found within the search window.
    // For other modes (Rolling, XY): free-runs immediately (no trigger search).
    // Blocks for approximately N * sample_interval_us µs.
    //
    // Returns true when the sweep represents a "successful" capture for
    // single-shot purposes: a real trigger crossing was found in Triggered
    // mode, or any completed sweep in the free-running modes.  Returns false
    // only when Triggered mode fell through to a free-run (no crossing found),
    // so single-shot keeps waiting for a genuine trigger.
    bool capture(const ScopeState& state, SampleBuffers& buf);
};
