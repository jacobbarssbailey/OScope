// Acquisition.h — Timer-triggered, DMA-based ADC acquisition (Milestone A).
//
// This is the option-4 acquisition: a hardware timer triggers ADC conversions
// at the timebase-derived rate, and eDMA streams the results into memory with
// zero CPU involvement during the sweep.  The pedvide Teensy ADC library
// (AnalogBufferDMA) owns the ADC/DMA/timer plumbing and the cache maintenance
// the M7 requires; this class is a thin controller over it.
//
// Interface is unchanged from the poll-based version so RunScreen and the modes
// are untouched:
//   update() — non-blocking; publishes a frame when a DMA buffer completes.
//   frame()  — the last complete frame, for rendering.
//
// MILESTONE B SCOPE (dual channel, free-running):
//   - Channel A on ADC0 / SIGNAL_A, channel B on ADC1 / SIGNAL_B, each with its
//     own DMA stream, both clocked by a timer at the same rate.  A frame is
//     published when both channels' DMA buffers have completed.
//   - No triggering yet: every completed pair of buffers is published as-is.
//   Software triggering (with optional pre-trigger) is Milestone C.
//
// The two ADC timers are started independently at the same frequency, so A[i]
// and B[i] carry a small constant sampling skew (fine for Y-t; acceptable for
// X-Y).  A single synchronized trigger is a later refinement if XY phase needs it.
//
// Sample rate: the timer runs at 1e6 / interval_us, where
//   interval_us = timebase_us_per_div * GridCols / N  (same mapping as before).
// Changing the timebase reconfigures the timer on the next update().
#pragma once

#include "modes/ScopeMode.h"
#include "ScopeState.h"
#include "Settings.h"

class Acquisition {
public:
    // Configure the ADC + DMA + timer.  Call once in setup().
    void begin();

    // Non-blocking: if a DMA buffer has completed, copy it into a frame and
    // publish it (returns true once per completed buffer).  Reconfigures the
    // sample timer when the timebase changes.
    bool update(const ScopeState& state, const Settings& settings);

    // Most recently completed frame, for rendering.  count == 0 until the first
    // buffer completes.
    const SampleBuffers& frame() const { return _buf[_show]; }

    // Whether the last published frame was trigger-aligned.  Always true in
    // Milestone A (free-running); real triggering returns in Milestone C.
    bool lastTriggered() const { return _lastTriggered; }

private:
    // Double buffer: one being shown, one being filled from the DMA buffer.
    SampleBuffers _buf[2];
    uint8_t _show = 0;
    uint8_t _fill = 1;
    bool    _lastTriggered = true;

    // Sample-timer state.
    bool     _started   = false;
    uint16_t _sTimebase = 0;   // timebase the timer is currently configured for

    // (Re)start the sample timer at the rate derived from timebase.
    void configureTimer(uint16_t timebase_us_per_div);
};
