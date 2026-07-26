// RingCapture.h — Continuous eDMA capture of one ADC channel into a ring.
//
// The DMA engine streams every timer-paced ADC conversion into a 4096-sample
// DMAMEM ring using hardware modulo addressing; it never stops and is never
// waited on.  Readers trail the hardware write cursor (TCD DADDR) behind a
// guard band, which makes torn reads structurally impossible — see
// docs/superpowers/specs/2026-07-13-acquisition-hardening-design.md.
//
// Write cursor: DADDR gives the position within the ring; a major-loop
// completion interrupt counts wraps.  totalWritten() combines the two into a
// 64-bit cumulative count, re-reading until stable so a wrap between the two
// reads cannot produce a torn value.
//
// The CPU never writes the ring (cache-coherency rule, commit a59ad4f);
// read() invalidates the dcache over exactly the region it copies out.
#pragma once
#include <stdint.h>
#include <DMAChannel.h>

class ADC;

class RingCapture {
public:
    // Samples per ring; power of two so ringIndex() is a mask.  The eDMA
    // destination-modulo field works in bytes, so the ring must also be aligned
    // to Size * sizeof(uint16_t) = 8192 bytes (see the DMAMEM declarations in
    // Acquisition.cpp).
    static constexpr uint32_t Size = 4096;

    // One-time setup: point the eDMA channel at this ADC's result register,
    // circular-destination into `ring`, enable ADC DMA requests.  adcNum is 0
    // (ADC1 hardware) or 1 (ADC2 hardware), matching the pedvide library's
    // module numbering used elsewhere in this codebase.
    void begin(ADC* adc, uint8_t adcNum, uint8_t pin, volatile uint16_t* ring);

    // (Re)start timer-paced conversions.  Split in two so a caller driving both
    // channels can get the two timers running as close together as possible:
    // whatever time separates the two run() calls is sample-count divergence
    // injected between the channels, and at a 1 µs interval that is samples, not
    // noise.  arm() does the slow part (stop the timer, select the pin) for both
    // channels first, leaving only the timer start between them.
    void arm();
    void run(uint32_t freqHz);

    // Convenience for a single channel where pairing does not apply.
    void start(uint32_t freqHz) { arm(); run(freqHz); }

    void stop();

    // Cumulative samples the DMA has written since begin().  Monotonic in
    // practice; see the note in the implementation about the sub-microsecond
    // window around a wrap, which can only return a slightly stale count, never
    // a torn or too-new one.
    uint64_t totalWritten();

    // Copy n samples starting at cumulative count `from` into dst, handling ring
    // wrap and dcache invalidation.  Caller must keep [from, from+n) within the
    // safe region (behind AcqCore::safeWatermark(totalWritten(), guard)).
    void read(uint64_t from, uint16_t* dst, uint16_t n);

private:
    DMAChannel         _dma;
    ADC*               _adc     = nullptr;
    uint8_t            _adcNum  = 0;
    uint8_t            _pin     = 0;
    volatile uint16_t* _ring    = nullptr;
    volatile uint32_t  _wraps   = 0;   // major-loop completions (ISR-owned)

    static RingCapture* s_instance[2];   // ISR trampolines, one per ADC
    static void isr0();
    static void isr1();

    // Increment before clearing so the window in which DADDR has wrapped but
    // _wraps has not is as short as possible (see totalWritten()).
    void onWrap() { ++_wraps; _dma.clearInterrupt(); }
};
