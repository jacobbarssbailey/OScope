// Acquisition.h — Timer-triggered, ring-based ADC acquisition.
//
// A hardware timer triggers ADC conversions at the timebase-derived rate and
// eDMA streams each channel into its own continuous RingCapture, with zero CPU
// involvement.  Nothing is waited on and no buffer changes hands: update() reads
// the newest window from behind the hardware write cursor, trailing it by a
// guard band, which is what makes torn reads impossible rather than merely
// detectable.  See RingCapture.h for the cursor contract.
//
// Interface is unchanged from the buffer-based version so RunScreen and the
// modes are untouched:
//   update() — non-blocking; publishes a frame when enough new samples exist.
//   frame()  — the last complete frame, for rendering.
// Rolling and X-Y follow-ups that want to consume the stream contiguously,
// rather than only its newest window, should use capA()/capB() and drive
// RingCapture::read() with their own cursor.
//
// Dual channel: channel A on ADC0 / SIGNAL_A, channel B on ADC1 / SIGNAL_B, each
// with its own ring and eDMA channel, both clocked at the same rate by
// independent timers.  Because the timers are independent, pairing is done on
// counts taken relative to per-channel origins sampled at the last reconfigure,
// and the residual A[i]/B[i] skew is reported as pairSkew (0-1 samples in
// practice; see docs/acq-characterization.md).
//
// Triggering is done in software over a linear scratch copy of the newest
// window.  That window holds CAPTURE = 2*N samples per channel — two screen
// widths — so a full N-sample display window can always be extracted starting
// at a found trigger:
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
//   interval_us = timebase_us_per_div * GridCols / N.
// Changing the timebase reconfigures both timers on the next update() and
// re-samples the pairing origins.
#pragma once

#include <AcqCore.h>

#include "AcqStats.h"
#include "Config.h"      // ACQ_DIAG
#include "modes/ScopeMode.h"
#include "RingCapture.h"
#include "ScopeState.h"
#include "Settings.h"

class Acquisition {
public:
    // Configure the ADC + DMA + timer.  Call once in setup().
    void begin();

    // Non-blocking: once CAPTURE new samples exist behind the guard band, copy
    // the newest window out of the rings and publish a frame (returns true when
    // it does).  Reconfigures both sample timers when the timebase changes.
    // This is the Triggered/XY path: each frame is an independent sweep, so the
    // CAPTURE-per-frame pacing is what keeps the cadence sane.
    bool update(const ScopeState& state, const Settings& settings);

    // Free-running path (Rolling and XY): publish the newest N-sample window
    // whenever any new samples have arrived, ungated by the CAPTURE accumulator.
    // Neither mode is trigger-anchored, so the newest window is always the frame
    // to show — for Rolling the overlap between consecutive windows reads as a
    // scroll, for XY it is simply the latest A/B pairs.  The frame rate is
    // therefore bounded by the display blit, not the sample rate, which is what
    // keeps it consistent at long timebases where update()'s "one frame per
    // CAPTURE samples" would crawl (6 fps at 10 ms/div).  Returns true when a
    // new window was published.
    bool updateFreeRunning(const ScopeState& state, const Settings& settings);

    // Read the newest `n` paired samples (inverted, display convention) straight
    // from the rings into caller buffers — for consumers that need a block wider
    // than the N-sample display frame, e.g. Spectrum's 256-point FFT.  Uses the
    // same origin-relative pairing and guard-band safe region as update(); does
    // NOT pace or publish.  Returns false until `n` samples exist.  Assumes the
    // timers are already configured this frame (update()/updateFreeRunning() runs
    // first each tick), so it takes no state.
    bool readNewestBlock(uint16_t* dstA, uint16_t* dstB, uint16_t n);

    // Most recently published frame, for rendering.  count == 0 until the rings
    // hold enough samples for the first window.
    const SampleBuffers& frame() const { return _buf[_show]; }

    // Whether the last published frame was trigger-aligned: true for a real
    // crossing (Triggered mode) or any free-running frame (Rolling/XY, or Auto
    // fallback with no crossing → false).  Used by single-shot.
    bool lastTriggered() const { return _lastTriggered; }

    // Capture-health counters (see AcqStats.h).  statsMutable() exists so the
    // 1 Hz reporter can reset per-window maxima.
    const AcqStats& stats() const { return _stats; }
    AcqStats&       statsMutable() { return _stats; }

    // Print a 1 Hz stats line to Serial, plus a per-cell aggregate line at each
    // checkpoint and at the end of a dwell.  A "cell" is one (timebase, mode)
    // pair held steady — the unit of the characterization protocol
    // (docs/acq-characterization.md).  Changing either one starts a new cell, so
    // knob sweeps never pollute a dwell.  Call once per loop; cheap no-op
    // between reports, and compiled out entirely unless ACQ_DIAG is set.
    void reportDiag(const ScopeState& state);

    // The underlying capture rings.  Rolling and X-Y follow-ups consume these
    // directly (contiguously, with their own cursor) rather than going through
    // update()'s newest-window read; unused on this branch.
    RingCapture& capA();
    RingCapture& capB();

private:
    // Acquisition health stats (tear detection, trigger misses, etc).
    AcqStats _stats;

    // Double buffer: one being shown, one being filled from the ring window.
    SampleBuffers _buf[2];
    uint8_t _show = 0;
    uint8_t _fill = 1;
    bool    _lastTriggered = true;

    // Sample-timer state.
    bool     _started   = false;
    uint32_t _sTimebase = 0;   // timebase the timer is currently configured for

    // Auto-mode trigger: consecutive frames with no crossing found.  Auto holds
    // the last triggered frame through brief misses and only free-runs once this
    // exceeds a threshold, so a single missed buffer doesn't flash unaligned.
    uint16_t _autoMissCount = 0;

    // Per-channel cumulative counts at the last reconfigure.  All pairing is
    // done relative to these, so the divergence a timer restart injects between
    // the two channels is absorbed once instead of accumulating forever.
    uint64_t _originA = 0;
    uint64_t _originB = 0;

    // Cumulative sample count at which the next frame may be produced.  The
    // rings run continuously, so without this the display would re-read the same
    // newest window every loop; pacing at one frame per CAPTURE new samples
    // keeps the frame cadence comparable to the Phase 1 baseline.
    uint64_t _nextProduceAt = 0;

    // Free-running path (Rolling/XY): the safe watermark at the last published
    // frame.  A new frame is published only once the watermark advances, so a
    // paused input holds the trace instead of re-blitting an identical window.
    uint64_t _freeLastSafe = 0;

    // Re-read and compare the published region to prove no tear occurred.  The
    // safe-region protocol makes tears structurally impossible, so this is
    // verification, not detection; ACQ_DIAG-only because it doubles the read
    // cost of every frame.
    static constexpr bool kDiagVerify = (ACQ_DIAG != 0);

    // 1 Hz diagnostics reporter state (deltas since the previous report).
    uint32_t _repLastMs      = 0;
    uint32_t _repLastFrames  = 0;
    uint32_t _repLastTears   = 0;
    uint32_t _repLastMisses  = 0;
    uint32_t _repLastWaits   = 0;
    uint32_t _repLastOver    = 0;

    // Aggregate for the cell currently being dwelt on.
    AcqCore::CellStats _cell;

    // Dwell target for one protocol cell, in seconds (2.5 minutes), the
    // checkpoint cadence for the aggregate line, and the shortest dwell worth
    // reporting when a cell is cut short (below this it is a knob transient).
    static constexpr uint32_t kCellTargetSecs     = 150;
    static constexpr uint32_t kCellCheckpointSecs = 30;
    static constexpr uint32_t kCellMinSecs        = 10;

    // Longest 1 Hz window still treated as one second (see diagWindowStale).
    static constexpr uint32_t kDiagWindowMaxMs = 2000;

    // Settle time after restarting both ADC timers, before the pairing origins
    // are sampled.  ~6x the ~157 µs transient measured in configureTimer's
    // comment, so the margin does not depend on getting that number exactly
    // right.
    static constexpr uint32_t kRestartSettleUs = 1000;

    // (Re)start the sample timer at the rate derived from timebase.
    void configureTimer(uint32_t timebase_us_per_div);

    // Reconfigure both sample timers on first run or a timebase change.  Shared
    // by update() and updateRolling() so the two paths agree on when the rings
    // are re-origined.
    void ensureConfigured(const ScopeState& state);

    // Print the aggregate line for the current cell, tagged with `status`.
    void printCell(const ScopeState& state, const char* status);

    // Restart the 1 Hz delta window at `now`, discarding the partial second
    // and the gap/tear transient a timebase or mode change produces.
    void rebaseDiagWindow(uint32_t now);
};
