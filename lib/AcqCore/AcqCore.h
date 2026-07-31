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

// ---- Ring cursor protocol -------------------------------------------------
// Cumulative 64-bit sample counts never wrap in practice (2^64 samples at
// 1 Msps is roughly 585,000 years); all reader/writer positions are counts,
// converted to ring offsets only at the memory access.

// Position of cumulative count within a power-of-two ring.
inline uint32_t ringIndex(uint64_t count, uint32_t ringSize) {
    return (uint32_t)(count & (uint64_t)(ringSize - 1));
}

// Newest cumulative count the reader may touch: the DMA write head minus a
// guard band (whole dcache lines that may still be landing).
inline uint64_t safeWatermark(uint64_t written, uint32_t guard) {
    return written > guard ? written - guard : 0;
}

// Base of the newest len-sample window ending at `safe`.  False until enough
// samples exist.
inline bool newestWindow(uint64_t safe, uint32_t len, uint64_t* base) {
    if (safe < len) return false;
    *base = safe - len;
    return true;
}

// ---- Pitch detection (YIN) ------------------------------------------------
// YIN fundamental-period estimator (de Cheveigné & Kawahara 2002).  Pure float
// math, no Arduino/CMSIS dependency, so it is unit-testable off-target.
//
//   x      : input samples x[0..n).  A DC offset is harmless — the difference
//            function cancels it — so callers need not remove the mean.
//   n      : window length (even).
//   diff   : caller scratch of length n/2 + 1 (reused for the difference
//            function and its cumulative-mean normalization).
//   thr    : absolute threshold on the normalized difference (0.10–0.15 typical;
//            lower = stricter/more confident).
//
// Returns the fundamental period in samples, parabolically interpolated for
// sub-sample precision, or -1 if no periodicity clears the threshold (silence or
// aperiodic input).  Convert to frequency as sampleRate / period.
inline float yinPeriod(const float* x, int n, float* diff, float thr) {
    const int tauMax = n / 2;

    // Step 1: squared-difference function d(tau).
    diff[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau) {
        float sum = 0.0f;
        for (int j = 0; j < tauMax; ++j) {
            const float d = x[j] - x[j + tau];
            sum += d * d;
        }
        diff[tau] = sum;
    }

    // Step 2: cumulative mean normalized difference d'(tau).
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau) {
        running += diff[tau];
        diff[tau] = (running > 0.0f) ? diff[tau] * (float)tau / running : 1.0f;
    }

    // Step 3: first tau below the absolute threshold, advanced to its local min.
    int tauEst = -1;
    for (int tau = 2; tau <= tauMax; ++tau) {
        if (diff[tau] < thr) {
            while (tau + 1 <= tauMax && diff[tau + 1] < diff[tau]) ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0) return -1.0f;

    // Step 4: parabolic interpolation of the minimum for sub-sample precision.
    float period = (float)tauEst;
    if (tauEst > 1 && tauEst < tauMax) {
        const float s0 = diff[tauEst - 1], s1 = diff[tauEst], s2 = diff[tauEst + 1];
        const float denom = s0 - 2.0f * s1 + s2;
        if (denom != 0.0f) period = (float)tauEst + 0.5f * (s0 - s2) / denom;
    }
    return period;
}

// ---- Characterization cells ----------------------------------------------

// One characterization cell: a (timebase, mode) pair held steady for the
// protocol's dwell time.  Folds the 1 Hz diagnostics deltas into cell totals
// and worst-second peaks so the reporter can print one line per cell that maps
// straight onto the protocol table (docs/acq-characterization.md).
struct CellStats {
    uint32_t timebase = 0;      // µs/div this cell is for
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
    uint32_t skewPeak = 0;      // worst A/B pair skew seen, in samples

    bool reported = false;      // the Done line has been printed for this cell

    // Begin a fresh cell.  Called whenever the timebase or mode changes, so a
    // knob sweep through intermediate steps never pollutes a dwell.
    void restart(uint32_t tb, uint8_t m) {
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

    // A/B skew is an instantaneous reading rather than a per-second delta, so it
    // is folded in as it is sampled (every update, not every report window).
    void noteSkew(uint32_t skew) { if (skew > skewPeak) skewPeak = skew; }

    // Mean frames/s over the cell, in tenths (integer math: no float printf).
    uint32_t fpsTenths() const { return secs ? (frames * 10) / secs : 0; }

    // Largest A/B skew still counted as meeting the design spec's "within ~1
    // sample": two, which is the jitter a healthy pair of independently-timed
    // rings shows over a 150 s dwell.
    static constexpr uint32_t kSkewLimit = 2;

    // The machine-checkable acceptance criteria: zero tears, zero overruns, and
    // A/B skew within tolerance.  Trigger misses are expected at short timebases
    // (the buffer spans less than one input period) and do not fail a cell.
    bool pass() const {
        return tears == 0 && overruns == 0 && skewPeak <= kSkewLimit;
    }
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
