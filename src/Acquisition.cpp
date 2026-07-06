// Acquisition.cpp — Timer-triggered DMA acquisition (Milestone A).
//
// The pedvide Teensy ADC library drives a hardware timer that triggers ADC
// conversions at a fixed rate; eDMA streams each conversion into a buffer.
// AnalogBufferDMA double-buffers and handles the M7 cache invalidation, so
// bufferLastData() hands back a coherent, complete buffer to publish.
//
// Voltage ↔ ADC mapping is unchanged (10-bit, 0..1023, mid-rail 512).
// Sample rate: timer frequency = 1e6 / interval_us,
//   interval_us = timebase_us_per_div * GridCols / N   (min 1 µs).

#include "Acquisition.h"
#include "Config.h"
#include "Theme.h"

#include <Arduino.h>
#include <ADC.h>
#include <ADC_util.h>
#include <AnalogBufferDMA.h>

static constexpr uint16_t N = SampleBuffers::N;   // 240

// DMA target buffers, double-buffered by AnalogBufferDMA — one pair per channel
// (A on ADC0, B on ADC1).  Must be in DMAMEM and 32-byte aligned so eDMA and the
// M7 data cache stay coherent.
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufA1[N];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufA2[N];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufB1[N];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) s_dmaBufB2[N];

static ADC s_adc;
static AnalogBufferDMA s_abdmaA(s_dmaBufA1, N, s_dmaBufA2, N);   // channel A, ADC0
static AnalogBufferDMA s_abdmaB(s_dmaBufB1, N, s_dmaBufB2, N);   // channel B, ADC1

// Inter-sample interval in µs from the current timebase (min 1 µs).
static uint32_t sampleIntervalUs(uint16_t timebase_us_per_div) {
    uint32_t interval = ((uint32_t)timebase_us_per_div * Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    return interval;
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

bool Acquisition::update(const ScopeState& state, const Settings& /*settings*/) {
    // (Re)configure the sample timers on first run or when the timebase changes.
    if (!_started || _sTimebase != state.timebase_us_per_div) {
        configureTimer(state.timebase_us_per_div);
        _sTimebase = state.timebase_us_per_div;
        _started   = true;
    }

    // Publish a frame only when BOTH channels have a completed DMA buffer, so
    // the two traces stay paired frame-for-frame.  At equal rates they complete
    // within microseconds of each other, so neither laps the other.
    if (!s_abdmaA.interrupted() || !s_abdmaB.interrupted()) {
        return false;
    }

    volatile uint16_t* srcA = s_abdmaA.bufferLastISRFilled();
    volatile uint16_t* srcB = s_abdmaB.bufferLastISRFilled();
    uint16_t nA = s_abdmaA.bufferCountLastISRFilled();
    uint16_t nB = s_abdmaB.bufferCountLastISRFilled();
    uint16_t n = nA < nB ? nA : nB;
    if (n > N) n = N;

    SampleBuffers& fb = _buf[_fill];
    for (uint16_t i = 0; i < n; ++i) {
        fb.ch[0][i] = (uint16_t)srcA[i];
        fb.ch[1][i] = (uint16_t)srcB[i];
    }
    fb.count = n;

    s_abdmaA.clearInterrupt();
    s_abdmaB.clearInterrupt();

    _lastTriggered = true;   // free-running: every buffer pair "succeeds"

    // Publish: swap fill/show.
    const uint8_t tmp = _show;
    _show = _fill;
    _fill = tmp;
    return true;
}
