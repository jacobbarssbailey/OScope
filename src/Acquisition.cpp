// Acquisition.cpp — Timer-triggered, dual-channel ring acquisition.
//
// A hardware timer triggers ADC conversions on both ADCs (channel A on ADC0,
// channel B on ADC1) at the timebase-derived rate; eDMA streams each channel
// into its own continuous RingCapture.  Nothing is ever waited on and no buffer
// changes hands: update() reads the newest CAPTURE-sample window from behind the
// hardware write cursor, which is what makes a torn read impossible rather than
// merely detectable (see RingCapture.h).
//
// The two rings are paced by independent timers, so pairing is done on counts
// taken relative to per-channel origins captured at the last reconfigure.
//
// update() extracts an N-sample display window — trigger-aligned in Triggered
// mode via a software edge search, or the first N samples otherwise — and
// publishes it via frame().
//
// Voltage ↔ ADC mapping: 10-bit, 0..1023, mid-rail 512.
// Sample rate: timer frequency = 1e6 / interval_us,
//   interval_us = timebase_us_per_div * GridCols / N   (min 1 µs).

#include "Acquisition.h"
#include "Config.h"
#include "Theme.h"

#include "Parameter.h"

#include "RingCapture.h"

#include <AcqCore.h>
#include <Arduino.h>
#include <ADC.h>
#include <ADC_util.h>

static constexpr uint16_t N       = SampleBuffers::N;   // 240 (display window)
static constexpr uint16_t CAPTURE = 2 * N;             // 480 samples per DMA buffer

// Nominal ADC count for 0 V input (mid-rail).
static constexpr int32_t ADC_MID = 512;

// Full-scale ADC count (10-bit).  Used to invert the hardware-inverted input.
static constexpr uint16_t kADCMax = 1023;

// Auto mode holds the last triggered frame through this many consecutive trigger
// misses before free-running, so a single missed buffer doesn't flash an
// unaligned frame.  At typical frame rates this is a fraction of a second.
static constexpr uint16_t kAutoFreerunMisses = 20;

// Continuous capture rings, one per channel (A on ADC0/SIGNAL_A, B on
// ADC1/SIGNAL_B).  DMAMEM keeps them out of the cached region the CPU writes;
// aligned(8192) is required by the eDMA destination-modulo field, which works
// in bytes (Size * sizeof(uint16_t)).
DMAMEM static volatile uint16_t __attribute__((aligned(8192))) s_ringA[RingCapture::Size];
DMAMEM static volatile uint16_t __attribute__((aligned(8192))) s_ringB[RingCapture::Size];

static ADC s_adc;
static RingCapture s_capA;
static RingCapture s_capB;

// How far the reader trails the DMA write head, in samples.  Two 32-byte cache
// lines' worth: enough that any line still being filled is behind us.
static constexpr uint32_t kGuard = 32;

// Inter-sample interval in µs from the current timebase (min 1 µs).
static uint32_t sampleIntervalUs(uint16_t timebase_us_per_div) {
    uint32_t interval = ((uint32_t)timebase_us_per_div * Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    return interval;
}

// Convert trigger_level_mv to an ADC count threshold, clamped to [0, 1023].
static uint16_t triggerADC(int16_t trigger_level_mv) {
    int32_t adc = ADC_MID + ((int32_t)trigger_level_mv * ADC_MID) / 10000;
    if (adc < 0)    adc = 0;
    if (adc > 1023) adc = 1023;
    return (uint16_t)adc;
}

// --------------------------------------------------------------------------

void Acquisition::begin() {
    // One-time ADC configuration for both ADCs.  Fast conversion/sampling to
    // keep up at short timebases; no hardware averaging (a scope wants raw
    // samples).
    s_adc.adc0->setAveraging(1);
    s_adc.adc0->setResolution(10);
    s_adc.adc0->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
    s_adc.adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED);

    s_adc.adc1->setAveraging(1);
    s_adc.adc1->setResolution(10);
    s_adc.adc1->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
    s_adc.adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::HIGH_SPEED);

    s_capA.begin(&s_adc, 0, SIGNAL_A, s_ringA);
    s_capB.begin(&s_adc, 1, SIGNAL_B, s_ringB);

    _started = false;   // configureTimer() runs on the first update()
}

RingCapture& Acquisition::capA() { return s_capA; }
RingCapture& Acquisition::capB() { return s_capB; }

void Acquisition::configureTimer(uint16_t timebase_us_per_div) {
    const uint32_t interval = sampleIntervalUs(timebase_us_per_div);
    uint32_t freq = 1000000UL / interval;
    if (freq == 0) freq = 1;

    // Point each ADC at its channel pin, then start both timers back to back.
    // The gap between the two run() calls is sample-count divergence injected
    // between the channels, so do the slow part (arm) for both first.
    s_capA.arm();
    s_capB.arm();
    s_capA.run(freq);
    s_capB.run(freq);

    // Re-zero the pairing origins.  Whatever divergence the restart above still
    // injects — plus anything accumulated by previous restarts — is absorbed
    // here: from now on A[originA + k] pairs with B[originB + k], and pairSkew
    // measures only genuine drift since this reconfigure.  Without this the two
    // rings' cumulative counts diverge a little on every timebase change and
    // never recover, which is what Phase 2 measured as ~80 samples of skew.
    _originA = s_capA.totalWritten();
    _originB = s_capB.totalWritten();

    // The rings hold samples taken at the previous rate; produce again as soon
    // as enough new ones exist rather than mixing two timebases into a frame.
    _nextProduceAt = 0;
}

bool Acquisition::update(const ScopeState& state, const Settings& settings) {
    // (Re)configure the sample timers on first run or when the timebase changes.
    // Trigger source/edge/level changes need no reconfigure — they only affect
    // the software search below and take effect on the next buffer.
    if (!_started || _sTimebase != state.timebase_us_per_div) {
        configureTimer(state.timebase_us_per_div);
        _sTimebase = state.timebase_us_per_div;
        _started   = true;
    }

    _diagVerify = settings.diag;

    // Both rings are paced by independent timers at the same rate, so pair them
    // by index at the lower of the two write heads.  Counts are taken relative
    // to the origins captured at the last reconfigure, so restart divergence is
    // already absorbed and what remains is genuine drift — the design spec's A/B
    // criterion, recorded rather than waited on.
    const uint64_t availA = s_capA.totalWritten() - _originA;
    const uint64_t availB = s_capB.totalWritten() - _originB;
    _stats.pairSkew = (uint32_t)((availA > availB) ? (availA - availB)
                                                  : (availB - availA));
    _cell.noteSkew(_stats.pairSkew);

    const uint64_t safe = AcqCore::safeWatermark((availA < availB) ? availA : availB,
                                                 kGuard);

    // The rings never stop, so pace production: one frame per CAPTURE new
    // samples, matching the old double-buffer cadence so frame rates stay
    // comparable to the Phase 1 baseline.
    if (safe < CAPTURE || safe < _nextProduceAt) return false;

    // Copy the newest CAPTURE-sample window out of the rings into linear
    // scratch, still raw (hardware-inverted).  Everything below is then plain
    // linear indexing with no wrap cases, exactly as the old buffer path was.
    // base is origin-relative; each ring is read at its own origin plus that
    // offset, which is what makes the two windows time-aligned.
    uint64_t base = 0;
    AcqCore::newestWindow(safe, CAPTURE, &base);

    // Resolve each channel's absolute read position exactly once.  Anything that
    // re-reads the same window (the diag verify below) must use these, not
    // recompute the arithmetic: keeping two copies of it is what let the verify
    // read drift onto an unrelated region when origins became nonzero.
    const uint64_t absA = _originA + base;
    const uint64_t absB = _originB + base;

    static uint16_t scratchA[CAPTURE];
    static uint16_t scratchB[CAPTURE];
    s_capA.read(absA, scratchA, CAPTURE);
    s_capB.read(absB, scratchB, CAPTURE);
    _nextProduceAt = safe + CAPTURE;

    // The input hardware inverts the signal: a HIGHER ADC count is a LOWER input
    // voltage.  Invert only on the copy into the (non-DMA) frame, and search the
    // still-raw scratch for the equivalent raw-space edge.  The scratch buffers
    // are ordinary cached RAM the CPU owns, so unlike the old DMA buffers they
    // could be normalized in place — kept raw so the search and the copy agree.

    // Decide the window start into the capture buffer.
    // Default (free-run / non-triggered): the first N samples.
    uint16_t start = 0;
    bool     triggered = true;
    bool     produce   = true;

    if (state.mode == Mode::Triggered) {
        // Search the trigger-source channel over the first N samples, which
        // leaves a full N-sample window after any crossing.  The scratch is
        // still raw (inverted), so a rising input edge is a FALLING raw edge
        // through the inverted threshold (kADCMax − thr).
        const uint16_t searchLen = N;
        const uint16_t* trigSrc  = (settings.trigSource == TrigSource::B)
                                   ? scratchB : scratchA;
        const uint16_t rawThr    = (uint16_t)(kADCMax - triggerADC(state.trigger_level_mv));
        const bool     rawRising = (settings.trigEdge != TrigEdge::Rising);

        const int t = AcqCore::findTrigger(trigSrc, searchLen, rawThr, rawRising);
        if (t < 0) ++_stats.trigMisses;
        if (t >= 0) {
            start     = (uint16_t)t;     // trigger at the left edge
            triggered = true;
            _autoMissCount = 0;
        } else if (settings.trigMode == TrigMode::Auto) {
            // Hold the last triggered frame through brief misses; only free-run
            // once the trigger has been absent for kAutoFreerunMisses frames, so
            // a single missed buffer doesn't flash an unaligned frame.
            if (_autoMissCount < kAutoFreerunMisses) {
                ++_autoMissCount;
                produce = false;         // hold last frame
            } else {
                start     = 0;           // sustained no-trigger: free-run
                triggered = false;
            }
        } else {
            produce   = false;           // Normal: hold last frame, wait
        }
    }

    if (produce) {
        // start is bounded by searchLen == N and the scratch holds CAPTURE == 2N,
        // so a full N-sample window always exists after the trigger.
        SampleBuffers& fb = _buf[_fill];
        for (uint16_t i = 0; i < N; ++i) {
            fb.ch[0][i] = (uint16_t)(kADCMax - scratchA[start + i]);
            fb.ch[1][i] = (uint16_t)(kADCMax - scratchB[start + i]);
        }
        fb.count = N;

        // Tear verification, not detection: trailing the write head by kGuard
        // makes a torn read structurally impossible, so re-reading the same
        // region must reproduce it.  Diag-only, since it doubles the read cost.
        if (_diagVerify) {
            static uint16_t verifyBuf[CAPTURE];
            s_capA.read(absA, verifyBuf, CAPTURE);
            if (AcqCore::checksum(verifyBuf, CAPTURE) !=
                AcqCore::checksum(scratchA, CAPTURE)) {
                ++_stats.tearEvents;
            }
        }

        _stats.noteConsume(micros());
        _lastTriggered = triggered;

        const uint8_t tmp = _show;
        _show = _fill;
        _fill = tmp;
    }

    return produce;
}

// Compact "[500 us/div ROLL]" tag identifying a characterization cell.  Both
// halves are reused from existing formatters so the log and the display agree.
static void cellTag(const ScopeState& state, char* b, uint8_t n) {
    char tb[16];
    parameterFor(EncoderParam::Timebase).format(state, tb, sizeof tb);
    snprintf(b, n, "[%s %s]", tb, modeName(state.mode));
}

void Acquisition::rebaseDiagWindow(uint32_t now) {
    _repLastMs     = now;
    _repLastFrames = _stats.framesProduced;
    _repLastTears  = _stats.tearEvents;
    _repLastMisses = _stats.trigMisses;
    _repLastWaits  = _stats.pairWaits;
    _repLastOver   = _stats.overruns;
    _stats.resetWindow();
    // lastConsumeUs deliberately survives: a gap that straddles two report
    // windows is still a real gap and must not be lost.  Only a cell change
    // clears it (see reportDiag), because the timer reconfigure there produces
    // one artificial gap that belongs to neither cell.
}

void Acquisition::printCell(const ScopeState& state, const char* status) {
    char tag[32];
    cellTag(state, tag, sizeof tag);

    const uint32_t fps    = _cell.fpsTenths();
    const uint32_t tearAvg = _cell.secs ? (_cell.tears * 10) / _cell.secs : 0;

    // One line per cell, in the order of the protocol table's Record: list.
    Serial.printf("cell: %s t=%lu/%lus %s fps=%lu.%lu tears=%lu(%lu.%lu/s avg,%lu/s pk) "
                  "miss=%lu(%lu/s pk) pairwait=%lu(%lu/s pk) over=%lu gapmax=%lums "
                  "skew=%lu pk %s\n",
        tag,
        (unsigned long)_cell.secs, (unsigned long)kCellTargetSecs, status,
        (unsigned long)(fps / 10), (unsigned long)(fps % 10),
        (unsigned long)_cell.tears,
        (unsigned long)(tearAvg / 10), (unsigned long)(tearAvg % 10),
        (unsigned long)_cell.tearPeak,
        (unsigned long)_cell.misses, (unsigned long)_cell.missPeak,
        (unsigned long)_cell.waits,  (unsigned long)_cell.waitPeak,
        (unsigned long)_cell.overruns,
        (unsigned long)(_cell.gapMaxUs / 1000),
        (unsigned long)_cell.skewPeak,
        _cell.pass() ? "PASS" : (_cell.overruns ? "FAIL:tears+over" : "FAIL:tears"));
}

void Acquisition::reportDiag(const ScopeState& state, const Settings& settings) {
    if (!settings.diag) return;
    const uint32_t now = millis();

    if (_repLastMs == 0) {   // first call: arm the window and open a cell
        _cell.restart(state.timebase_us_per_div, (uint8_t)state.mode);
        rebaseDiagWindow(now);
        _stats.lastConsumeUs = 0;
        return;
    }

    // A timebase or mode change ends the dwell immediately, mid-second: the
    // knob has moved, so nothing after this point belongs to the old cell.
    if (state.timebase_us_per_div != _cell.timebase ||
        (uint8_t)state.mode != _cell.mode) {
        if (!_cell.reported &&
            AcqCore::cellPartialWorth(_cell.secs, kCellMinSecs, kCellTargetSecs)) {
            printCell(state, "CUT SHORT");
        }
        _cell.restart(state.timebase_us_per_div, (uint8_t)state.mode);
        rebaseDiagWindow(now);
        _stats.lastConsumeUs = 0;   // discard the reconfigure's artificial gap
        return;
    }

    const uint32_t elapsed = now - _repLastMs;
    if (elapsed < 1000) return;

    // Reporting stops whenever the run screen is not live (settings menu, frozen
    // trace, Diag toggled off and back on).  Discard that window rather than
    // recording many seconds of deltas as one second.
    if (AcqCore::diagWindowStale(elapsed, kDiagWindowMaxMs)) {
        rebaseDiagWindow(now);
        _stats.lastConsumeUs = 0;
        return;
    }

    // Buffer completion rate at this timebase: one buffer per CAPTURE samples.
    // This is the DMA's rate, not an achievable frame rate — at short timebases
    // buffers complete far faster than the display can consume them, which is
    // the tear mechanism rather than a fault in the reader.
    const uint32_t interval = ((uint32_t)_sTimebase * Theme::GridCols)
                              / SampleBuffers::N;
    const uint32_t bufs = (1000000UL / (interval < 1 ? 1 : interval)) / CAPTURE;

    const uint32_t framesD = _stats.framesProduced - _repLastFrames;
    const uint32_t tearsD  = _stats.tearEvents     - _repLastTears;
    const uint32_t missD   = _stats.trigMisses     - _repLastMisses;
    const uint32_t waitD   = _stats.pairWaits      - _repLastWaits;
    const uint32_t overD   = _stats.overruns       - _repLastOver;

    _cell.addSecond(framesD, tearsD, missD, waitD, overD, _stats.gapMaxUs);

    char tag[32];
    cellTag(state, tag, sizeof tag);
    Serial.printf("acq: %s t=%lus fps=%lu bufs=%lu tears=%lu(%lu) miss=%lu pairwait=%lu over=%lu gap=%lums skew=%lu\n",
        tag,
        (unsigned long)_cell.secs,
        (unsigned long)framesD,
        (unsigned long)bufs,
        (unsigned long)tearsD,
        (unsigned long)_cell.tears,
        (unsigned long)missD,
        (unsigned long)waitD,
        (unsigned long)overD,
        (unsigned long)(_stats.gapMaxUs / 1000),
        (unsigned long)_stats.pairSkew);

    switch (AcqCore::cellReport(_cell.secs, kCellTargetSecs, kCellCheckpointSecs)) {
        case AcqCore::CellReport::Progress: printCell(state, "...");  break;
        case AcqCore::CellReport::Done:     printCell(state, "DONE");
                                            _cell.reported = true;    break;
        case AcqCore::CellReport::None:                               break;
    }

    rebaseDiagWindow(now);
}
