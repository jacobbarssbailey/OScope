// AcqCore.h — Pure acquisition logic, unit-testable off-target (no Arduino
// dependencies).  Everything here operates on plain sample arrays and 64-bit
// cumulative sample counts; nothing touches hardware.
#pragma once
#include <stdint.h>

namespace AcqCore {

// Scan src[1..searchLen] for the first edge crossing of `thr` in the given
// direction.  Returns the crossing index, or -1 if none.  Pointer is
// const-volatile so raw DMA buffers and plain arrays are both accepted.
inline int findTrigger(const volatile uint16_t* src, uint16_t searchLen,
                       uint16_t thr, bool rising) {
    for (uint16_t t = 1; t <= searchLen; ++t) {
        const bool cross = rising ? (src[t - 1] < thr && src[t] >= thr)
                                  : (src[t - 1] > thr && src[t] <= thr);
        if (cross) return (int)t;
    }
    return -1;
}

// 16-bit additive checksum over n samples.  Used as a tear detector: sum a
// DMA region before and after copying it out; a mismatch means the DMA engine
// overwrote the region mid-read.  Not cryptographic — collisions are
// possible but vanishingly unlikely to hide a real tear (which changes many
// samples).
inline uint16_t checksum(const volatile uint16_t* src, uint16_t n) {
    uint16_t s = 0;
    for (uint16_t i = 0; i < n; ++i) s = (uint16_t)(s + src[i]);
    return s;
}

// ---- Characterization cells ----------------------------------------------

// One characterization cell: a (timebase, mode) pair held steady for the
// protocol's dwell time.  Folds the 1 Hz diagnostics deltas into cell totals
// and worst-second peaks so the reporter can print one line per cell that maps
// straight onto the protocol table (docs/acq-characterization.md).
struct CellStats {
    uint16_t timebase = 0;      // µs/div this cell is for
    uint8_t  mode     = 0xFF;   // ScopeState Mode, as a plain byte (no Arduino dep)

    uint32_t secs     = 0;
    uint32_t frames   = 0;
    uint32_t tears    = 0;
    uint32_t misses   = 0;
    uint32_t waits    = 0;
    uint32_t overruns = 0;

    uint32_t tearPeak = 0;      // worst single second, not the last one
    uint32_t missPeak = 0;
    uint32_t waitPeak = 0;
    uint32_t gapMaxUs = 0;      // worst inter-frame gap anywhere in the cell

    bool reported = false;      // the Done line has been printed for this cell

    // Begin a fresh cell.  Called whenever the timebase or mode changes, so a
    // knob sweep through intermediate steps never pollutes a dwell.
    void restart(uint16_t tb, uint8_t m) {
        *this = CellStats{};
        timebase = tb;
        mode     = m;
    }

    // Fold in one 1 Hz report: per-second deltas plus that window's gap max.
    void addSecond(uint32_t framesD, uint32_t tearsD, uint32_t missD,
                   uint32_t waitD, uint32_t overD, uint32_t gapUs) {
        ++secs;
        frames   += framesD;
        tears    += tearsD;
        misses   += missD;
        waits    += waitD;
        overruns += overD;
        if (tearsD > tearPeak) tearPeak = tearsD;
        if (missD  > missPeak) missPeak = missD;
        if (waitD  > waitPeak) waitPeak = waitD;
        if (gapUs  > gapMaxUs) gapMaxUs = gapUs;
    }

    // Mean frames/s over the cell, in tenths (integer math: no float printf).
    uint32_t fpsTenths() const { return secs ? (frames * 10) / secs : 0; }

    // The two acceptance criteria that are machine-checkable: zero tears and
    // zero overruns.  Trigger misses are expected at short timebases (the
    // buffer spans less than one input period) and do not fail a cell.
    bool pass() const { return tears == 0 && overruns == 0; }
};

enum class CellReport : uint8_t { None, Progress, Done };

// When the aggregate line should be printed: a checkpoint every `every`
// seconds so a bench operator can see progress, one Done line exactly at
// `target`, then silence (the dwell is over; further seconds are just the
// knob not having moved yet).
inline CellReport cellReport(uint32_t secs, uint32_t target, uint32_t every) {
    if (secs == 0 || secs > target) return CellReport::None;
    if (secs == target)             return CellReport::Done;
    if (every && secs % every == 0) return CellReport::Progress;
    return CellReport::None;
}

// Whether an interrupted cell deserves a line of its own: long enough to be a
// real dwell attempt, but short of the target (a completed cell already
// printed Done).  Keeps knob-sweep transients out of the log.
inline bool cellPartialWorth(uint32_t secs, uint32_t minSecs, uint32_t target) {
    return secs >= minSecs && secs < target;
}

// Whether a 1 Hz window is too old to record as one second.  Reporting only
// runs on the run screen with the trace live, so a trip to the settings menu or
// a stopped trace leaves a multi-second hole whose accumulated deltas would
// otherwise land in a single second and blow up that cell's peaks.
inline bool diagWindowStale(uint32_t elapsedMs, uint32_t maxMs) {
    return elapsedMs > maxMs;
}

}  // namespace AcqCore
