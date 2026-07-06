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

#include <Arduino.h>
#include <ADC.h>
#include <ADC_util.h>
#include <AnalogBufferDMA.h>

static constexpr uint16_t N       = SampleBuffers::N;   // 240 (display window)
static constexpr uint16_t CAPTURE = 2 * N;             // 480 samples per DMA buffer

// Nominal ADC count for 0 V input (mid-rail).
static constexpr int32_t ADC_MID = 512;

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

// Scan src[1..searchLen] for the first edge crossing of `thr` in the given
// direction.  Returns the crossing index, or -1 if none.
static int findTrigger(volatile uint16_t* src, uint16_t searchLen,
                       uint16_t thr, bool rising) {
    for (uint16_t t = 1; t <= searchLen; ++t) {
        const bool cross = rising ? (src[t - 1] < thr && src[t] >= thr)
                                  : (src[t - 1] > thr && src[t] <= thr);
        if (cross) return (int)t;
    }
    return -1;
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
    if (!s_abdmaA.interrupted() || !s_abdmaB.interrupted()) {
        return false;
    }

    volatile uint16_t* srcA = s_abdmaA.bufferLastISRFilled();
    volatile uint16_t* srcB = s_abdmaB.bufferLastISRFilled();
    const uint16_t nA = s_abdmaA.bufferCountLastISRFilled();
    const uint16_t nB = s_abdmaB.bufferCountLastISRFilled();
    const uint16_t cap = nA < nB ? nA : nB;   // usable samples in this buffer pair

    // Decide the window start into the capture buffer.
    // Default (free-run / non-triggered): the first N samples.
    uint16_t start = 0;
    bool     triggered = true;
    bool     produce   = true;

    if (state.mode == Mode::Triggered) {
        // Search the trigger-source channel over the first N samples, leaving a
        // full N-sample window after any crossing.
        const uint16_t searchLen = (cap > N) ? N : (cap ? (uint16_t)(cap - 1) : 0);
        volatile uint16_t* trigSrc = (settings.trigSource == TrigSource::B) ? srcB : srcA;
        const uint16_t thr    = triggerADC(state.trigger_level_mv);
        const bool     rising = (settings.trigEdge == TrigEdge::Rising);

        const int t = findTrigger(trigSrc, searchLen, thr, rising);
        if (t >= 0) {
            start     = (uint16_t)t;     // trigger at the left edge
            triggered = true;
        } else if (settings.trigMode == TrigMode::Auto) {
            start     = 0;               // free-run this frame
            triggered = false;
        } else {
            produce   = false;           // Normal: hold last frame, wait
        }
    }

    if (produce) {
        // Copy the N-sample window (clamped to what the buffer holds).
        uint16_t n = N;
        if (start + n > cap) n = (start < cap) ? (uint16_t)(cap - start) : 0;

        SampleBuffers& fb = _buf[_fill];
        for (uint16_t i = 0; i < n; ++i) {
            fb.ch[0][i] = (uint16_t)srcA[start + i];
            fb.ch[1][i] = (uint16_t)srcB[start + i];
        }
        fb.count = n;
        _lastTriggered = triggered;

        // Publish: swap fill/show.
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
