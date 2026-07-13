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

}  // namespace AcqCore
