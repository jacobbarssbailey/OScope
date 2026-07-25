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

#include <AcqCore.h>

#include "AcqStats.h"
#include "modes/ScopeMode.h"
#include "RingCapture.h"
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

    // Print a 1 Hz stats line to Serial while diagnostics are enabled, plus a
    // per-cell aggregate line at each checkpoint and at the end of a dwell.
    // A "cell" is one (timebase, mode) pair held steady — the unit of the
    // characterization protocol (docs/acq-characterization.md).  Changing
    // either one starts a new cell, so knob sweeps never pollute a dwell.
    // Call once per loop; cheap no-op between reports.
    void reportDiag(const ScopeState& state, const Settings& settings);

    // Dwell target for one protocol cell, in seconds (2.5 minutes).
    static constexpr uint32_t kCellTargetSecs = 150;

    // Current cell's running totals, for the on-screen diagnostics overlay.
    const AcqCore::CellStats& cell() const { return _cell; }

    // The underlying capture rings.  Rolling and X-Y follow-ups consume these
    // directly (contiguously, with their own cursor) rather than going through
    // update()'s newest-window read; unused on this branch.
    RingCapture& capA();
    RingCapture& capB();

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

    // Cumulative sample count at which the next frame may be produced.  The
    // rings run continuously, so without this the display would re-read the same
    // newest window every loop; pacing at one frame per CAPTURE new samples
    // keeps the frame cadence comparable to the Phase 1 baseline.
    uint64_t _nextProduceAt = 0;

    // Re-read and compare the published region to prove no tear occurred.  The
    // safe-region protocol makes tears structurally impossible, so this is
    // verification, not detection; diag-only because it doubles the read cost.
    bool _diagVerify = false;

    // 1 Hz diagnostics reporter state (deltas since the previous report).
    uint32_t _repLastMs      = 0;
    uint32_t _repLastFrames  = 0;
    uint32_t _repLastTears   = 0;
    uint32_t _repLastMisses  = 0;
    uint32_t _repLastWaits   = 0;
    uint32_t _repLastOver    = 0;

    // Aggregate for the cell currently being dwelt on.
    AcqCore::CellStats _cell;

    // Checkpoint cadence for the aggregate line, and the shortest dwell worth
    // reporting when a cell is cut short (below this it is a knob transient).
    static constexpr uint32_t kCellCheckpointSecs = 30;
    static constexpr uint32_t kCellMinSecs        = 10;

    // Longest 1 Hz window still treated as one second (see diagWindowStale).
    static constexpr uint32_t kDiagWindowMaxMs = 2000;

    // (Re)start the sample timer at the rate derived from timebase.
    void configureTimer(uint16_t timebase_us_per_div);

    // Print the aggregate line for the current cell, tagged with `status`.
    void printCell(const ScopeState& state, const char* status);

    // Restart the 1 Hz delta window at `now`, discarding the partial second
    // and the gap/tear transient a timebase or mode change produces.
    void rebaseDiagWindow(uint32_t now);
};
