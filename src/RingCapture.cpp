// RingCapture.cpp — see RingCapture.h for the ring/cursor contract.
//
// Register and macro choices here are mirrored from the working library rather
// than derived: AnalogBufferDMA.cpp's __IMXRT1062__ path is the reference for
// the DMA source register, the DMAMUX source, and the ADC-side enable sequence
// (continuousMode() then enableDMA()).  The timer pacing is the same pedvide
// API call Acquisition::configureTimer already uses.
#include "RingCapture.h"

#include <AcqCore.h>
#include <ADC.h>
#include <Arduino.h>
#include <string.h>

// Mirrored from AnalogBufferDMA.cpp (__IMXRT1062__ section): the ADC result
// register and DMAMUX request line for each ADC module.  Note the off-by-one
// in the names — the library's ADC module 0 is the chip's ADC1.
#define SOURCE_ADC_0 ADC1_R0
#define DMAMUX_ADC_0 DMAMUX_SOURCE_ADC1
#define SOURCE_ADC_1 ADC2_R0
#define DMAMUX_ADC_1 DMAMUX_SOURCE_ADC2

RingCapture* RingCapture::s_instance[2] = {nullptr, nullptr};

// The library follows each DMA ISR with a DSB on this core; mirror that.
void RingCapture::isr0() {
    if (s_instance[0]) s_instance[0]->onWrap();
    asm("DSB");
}

void RingCapture::isr1() {
    if (s_instance[1]) s_instance[1]->onWrap();
    asm("DSB");
}

void RingCapture::begin(ADC* adc, uint8_t adcNum, uint8_t pin,
                        volatile uint16_t* ring) {
    _adc = adc; _adcNum = adcNum; _pin = pin; _ring = ring;
    s_instance[adcNum] = this;

    _dma.begin();
    if (adcNum == 0) {
        _dma.source((volatile uint16_t&)SOURCE_ADC_0);
        _dma.triggerAtHardwareEvent(DMAMUX_ADC_0);
    } else {
        _dma.source((volatile uint16_t&)SOURCE_ADC_1);
        _dma.triggerAtHardwareEvent(DMAMUX_ADC_1);
    }

    // Byte length, not sample count: destinationCircular() derives both the
    // minor-loop count and the destination-modulo field from it.
    _dma.destinationCircular(_ring, Size * sizeof(uint16_t));

    // One major-loop completion == one full lap of the ring.
    _dma.interruptAtCompletion();
    _dma.attachInterrupt(adcNum == 0 ? isr0 : isr1);
    _dma.enable();

    ADC_Module* m = adc->adc[adcNum];
    m->continuousMode();
    m->enableDMA();
}

void RingCapture::arm() {
    ADC_Module* m = _adc->adc[_adcNum];
    m->stopTimer();
    m->startSingleRead(_pin);
}

void RingCapture::run(uint32_t freqHz) {
    _adc->adc[_adcNum]->startTimer(freqHz);
}

void RingCapture::stop() {
    _adc->adc[_adcNum]->stopTimer();
}

uint64_t RingCapture::totalWritten() {
    // DADDR advances one sample at a time and wraps in hardware; _wraps counts
    // major-loop completions.  Re-read until _wraps is stable so a wrap landing
    // between the two loads cannot combine an old lap count with a fresh offset.
    //
    // The ISR still lags the hardware wrap by its own latency (microseconds
    // against a 4096-sample lap).  In that window this returns a count up to one
    // lap stale, which makes the reader trail further behind the head — never
    // ahead of it — so a stale count costs freshness, not integrity.
    uint32_t wraps1, offset;
    do {
        wraps1 = _wraps;
        offset = ((uint32_t)_dma.TCD->DADDR - (uint32_t)(uintptr_t)_ring)
                 / sizeof(uint16_t);
    } while (wraps1 != _wraps);
    return (uint64_t)wraps1 * Size + offset;
}

void RingCapture::read(uint64_t from, uint16_t* dst, uint16_t n) {
    const uint32_t idx   = AcqCore::ringIndex(from, Size);
    const uint16_t first = (uint16_t)((idx + n <= Size) ? n : (Size - idx));

    // Discard rather than flush: the CPU never writes the ring, so there is
    // nothing dirty to write back, and a writeback would race the DMA.
    arm_dcache_delete((void*)(_ring + idx), first * sizeof(uint16_t));
    memcpy(dst, (const void*)(_ring + idx), first * sizeof(uint16_t));

    if (first < n) {   // wrapped: remainder from the ring start
        const uint16_t rest = (uint16_t)(n - first);
        arm_dcache_delete((void*)_ring, rest * sizeof(uint16_t));
        memcpy(dst + first, (const void*)_ring, rest * sizeof(uint16_t));
    }
}
