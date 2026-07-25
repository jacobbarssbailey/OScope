// Acquisition.cpp — Timer-triggered, dual-channel DMA acquisition.
//
// A hardware timer triggers ADC conversions on both ADCs (channel A on ADC0,
// channel B on ADC1) at the timebase-derived rate; eDMA streams each channel
// into its own CAPTURE-sample double buffer.  The pedvide Teensy ADC library
// (AnalogBufferDMA) owns the DMA plumbing and the M7 cache invalidation, and
// bufferLastISRFilled() hands back a coherent, complete buffer.
//
// update() consumes a completed buffer pair, extracts an N-sample display
// window — trigger-aligned in Triggered mode via a software edge search over
// the buffer, or the first N samples otherwise — and publishes it via frame().
//
// Voltage ↔ ADC mapping: 10-bit, 0..1023, mid-rail 512.
// Sample rate: timer frequency = 1e6 / interval_us,
//   interval_us = timebase_us_per_div * GridCols / N   (min 1 µs).

#include "Acquisition.h"
#include "Config.h"
#include "Theme.h"

#include "Parameter.h"

#include <AcqCore.h>
#include <Arduino.h>
#include <ADC.h>
#include <ADC_util.h>
#include <AnalogBufferDMA.h>

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

// DMA target buffers, double-buffered by AnalogBufferDMA — one pair per channel
// (A on ADC0, B on ADC1).  Each buffer holds CAPTURE samples so a full N-sample
// window can be extracted around a found trigger.  Must be in DMAMEM and 32-byte
// aligned so eDMA and the M7 data cache stay coherent.
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufA1[CAPTURE];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufA2[CAPTURE];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufB1[CAPTURE];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufB2[CAPTURE];

static ADC s_adc;
static AnalogBufferDMA s_abdmaA(s_dmaBufA1, CAPTURE, s_dmaBufA2, CAPTURE);  // ch A, ADC0
static AnalogBufferDMA s_abdmaB(s_dmaBufB1, CAPTURE, s_dmaBufB2, CAPTURE);  // ch B, ADC1

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

    s_abdmaA.init(&s_adc, ADC_0);
    s_abdmaB.init(&s_adc, ADC_1);

    _started = false;   // configureTimer() runs on the first update()
}

void Acquisition::configureTimer(uint16_t timebase_us_per_div) {
    const uint32_t interval = sampleIntervalUs(timebase_us_per_div);
    uint32_t freq = 1000000UL / interval;
    if (freq == 0) freq = 1;

    // Point each ADC at its channel pin and (re)start its timer at the same
    // rate.  Timer-triggered conversions stream into each channel's DMA buffer.
    s_adc.adc0->stopTimer();
    s_adc.adc1->stopTimer();
    s_adc.adc0->startSingleRead(SIGNAL_A);
    s_adc.adc1->startSingleRead(SIGNAL_B);
    s_adc.adc0->startTimer(freq);
    s_adc.adc1->startTimer(freq);
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

    // Consume only when BOTH channels have a completed DMA buffer, so the two
    // traces stay paired.  At equal rates they complete within microseconds of
    // each other, so neither laps the other.
    const bool readyA = s_abdmaA.interrupted();
    const bool readyB = s_abdmaB.interrupted();
    if (!readyA || !readyB) {
        if (readyA != readyB) ++_stats.pairWaits;
        return false;
    }

    volatile uint16_t* srcA = s_abdmaA.bufferLastISRFilled();
    volatile uint16_t* srcB = s_abdmaB.bufferLastISRFilled();
    const uint16_t nA = s_abdmaA.bufferCountLastISRFilled();
    const uint16_t nB = s_abdmaB.bufferCountLastISRFilled();
    const uint16_t cap = nA < nB ? nA : nB;   // usable samples in this buffer pair

    // The input hardware inverts the signal: a HIGHER ADC count is a LOWER input
    // voltage.  We must NOT normalize the buffer in place — it lives in cached
    // RAM2, and the ADC library treats it as read-only (its completion ISR does
    // no cache maintenance on Teensy 4).  A CPU write dirties cache lines that
    // later write back over freshly DMA'd samples → intermittent stale values.
    // Instead we invert only on the copy into the (non-DMA) frame, and search
    // the still-raw buffer for the equivalent raw-space edge.

    // Decide the window start into the capture buffer.
    // Default (free-run / non-triggered): the first N samples.
    uint16_t start = 0;
    bool     triggered = true;
    bool     produce   = true;

    if (state.mode == Mode::Triggered) {
        // Search the trigger-source channel over the first N samples, leaving a
        // full N-sample window after any crossing.  The buffer is still raw
        // (inverted), so a rising input edge is a FALLING raw edge through the
        // inverted threshold (kADCMax − thr).
        const uint16_t searchLen = (cap > N) ? N : (cap ? (uint16_t)(cap - 1) : 0);
        volatile uint16_t* trigSrc = (settings.trigSource == TrigSource::B) ? srcB : srcA;
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
        uint16_t n = N;
        if (start + n > cap) n = (start < cap) ? (uint16_t)(cap - start) : 0;

        // Tear detector: checksum the source region, copy, invalidate the
        // cache over the region, checksum again.  A mismatch means the DMA
        // engine lapped us and rewrote the region mid-copy.
        const uint16_t sumA = AcqCore::checksum(srcA + start, n);
        const uint16_t sumB = AcqCore::checksum(srcB + start, n);

        SampleBuffers& fb = _buf[_fill];
        for (uint16_t i = 0; i < n; ++i) {
            fb.ch[0][i] = (uint16_t)(kADCMax - srcA[start + i]);
            fb.ch[1][i] = (uint16_t)(kADCMax - srcB[start + i]);
        }
        fb.count = n;

        arm_dcache_delete((void*)(srcA + start), n * sizeof(uint16_t));
        arm_dcache_delete((void*)(srcB + start), n * sizeof(uint16_t));
        if (AcqCore::checksum(srcA + start, n) != sumA ||
            AcqCore::checksum(srcB + start, n) != sumB) {
            ++_stats.tearEvents;
        }

        _stats.noteConsume(micros());
        _lastTriggered = triggered;

        const uint8_t tmp = _show;
        _show = _fill;
        _fill = tmp;
    }

    // Always release the buffers so the next capture is fresh (esp. Normal mode
    // hold, which would otherwise re-scan the same stale buffer forever).
    s_abdmaA.clearInterrupt();
    s_abdmaB.clearInterrupt();

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
                  "miss=%lu(%lu/s pk) pairwait=%lu(%lu/s pk) over=%lu gapmax=%lums %s\n",
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
    Serial.printf("acq: %s t=%lus fps=%lu bufs=%lu tears=%lu(%lu) miss=%lu pairwait=%lu over=%lu gap=%lums\n",
        tag,
        (unsigned long)_cell.secs,
        (unsigned long)framesD,
        (unsigned long)bufs,
        (unsigned long)tearsD,
        (unsigned long)_cell.tears,
        (unsigned long)missD,
        (unsigned long)waitD,
        (unsigned long)overD,
        (unsigned long)(_stats.gapMaxUs / 1000));

    switch (AcqCore::cellReport(_cell.secs, kCellTargetSecs, kCellCheckpointSecs)) {
        case AcqCore::CellReport::Progress: printCell(state, "...");  break;
        case AcqCore::CellReport::Done:     printCell(state, "DONE");
                                            _cell.reported = true;    break;
        case AcqCore::CellReport::None:                               break;
    }

    rebaseDiagWindow(now);
}
