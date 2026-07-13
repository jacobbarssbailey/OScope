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
// Dual channel: channel A on ADC0 / SIGNAL_A, channel B on ADC1 / SIGNAL_B, each
// with its own DMA stream, both clocked by a timer at the same rate.  The two
// timers run independently at the same frequency, so A[i]/B[i] carry a small
// constant sampling skew (fine for Y-t; acceptable for X-Y).
//
// Triggering (Milestone C) is done in software over the captured buffer.  Each
// DMA buffer holds CAPTURE = 2*N samples per channel — two screen widths — so a
// full N-sample display window can be extracted starting at a found trigger:
//   Triggered — scan the trigger-source channel (settings.trigSource) in the
//     first N samples for a settings.trigEdge crossing of trigger_level_mv.
//     On a hit, both channels' [t, t+N) window is published (trigger at the left
//     edge).  If none is found: Auto publishes the first N samples (free-run);
//     Normal holds the last frame (no publish) and waits.
//   Rolling / XY — no trigger; the first N samples are published.
// Pre-trigger (showing samples before the edge) is a trivial future tweak: shift
// the window start to t - pretrigger.
//
// Sample rate: the timer runs at 1e6 / interval_us, where
//   interval_us = timebase_us_per_div * GridCols / N  (same mapping as before).
// Changing the timebase reconfigures the timer on the next update().
#pragma once

#include "AcqStats.h"
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

    // Whether the last published frame was trigger-aligned: true for a real
    // crossing (Triggered mode) or any free-running frame (Rolling/XY, or Auto
    // fallback with no crossing → false).  Used by single-shot.
    bool lastTriggered() const { return _lastTriggered; }

    // Capture-health counters (see AcqStats.h).  statsMutable() exists so the
    // 1 Hz reporter can reset per-window maxima.
    const AcqStats& stats() const { return _stats; }
    AcqStats&       statsMutable() { return _stats; }

    // Print a 1 Hz stats line to Serial while diagnostics are enabled.
    // Call once per loop; cheap no-op between reports.
    void reportDiag(bool enabled);

private:
    // Acquisition health stats (tear detection, trigger misses, etc).
    AcqStats _stats;

    // Double buffer: one being shown, one being filled from the DMA buffer.
    SampleBuffers _buf[2];
    uint8_t _show = 0;
    uint8_t _fill = 1;
    bool    _lastTriggered = true;

    // Sample-timer state.
    bool     _started   = false;
    uint16_t _sTimebase = 0;   // timebase the timer is currently configured for

    // Auto-mode trigger: consecutive frames with no crossing found.  Auto holds
    // the last triggered frame through brief misses and only free-runs once this
    // exceeds a threshold, so a single missed buffer doesn't flash unaligned.
    uint16_t _autoMissCount = 0;

    // 1 Hz diagnostics reporter state (deltas since the previous report).
    uint32_t _repLastMs      = 0;
    uint32_t _repLastFrames  = 0;
    uint32_t _repLastTears   = 0;
    uint32_t _repLastMisses  = 0;
    uint32_t _repLastWaits   = 0;
    uint32_t _repLastOver    = 0;

    // (Re)start the sample timer at the rate derived from timebase.
    void configureTimer(uint16_t timebase_us_per_div);
};
