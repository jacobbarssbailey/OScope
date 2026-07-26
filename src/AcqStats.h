// AcqStats.h — Acquisition health counters (characterization + regression).
//
// Cumulative event counters plus a per-report-window gap maximum.  Collected
// unconditionally (they are a handful of increments per frame — negligible),
// reported only in ACQ_DIAG builds.  Kept permanently so future
// capture regressions are measurable, not mysterious.
#pragma once
#include <stdint.h>

struct AcqStats {
    // Cumulative since boot.
    uint32_t framesProduced = 0;  // update() published a frame
    uint32_t tearEvents     = 0;  // DMA overwrote a buffer while we copied it
    uint32_t trigMisses     = 0;  // Triggered mode: buffer had no edge
    uint32_t pairWaits      = 0;  // polls where exactly one channel was ready
    uint32_t overruns       = 0;  // Phase 2: reader resynced after losing data

    // Instantaneous A/B write-cursor skew in samples (Phase 2).  The two rings
    // are paced by independent timers at the same rate, so this is the design
    // spec's "within ~1 sample" criterion made measurable.
    uint32_t pairSkew       = 0;

    // Max gap between consumed frames (µs) within the current report window;
    // the 1 Hz reporter prints and resets it via resetWindow().
    uint32_t gapMaxUs      = 0;
    uint32_t lastConsumeUs = 0;

    void noteConsume(uint32_t nowUs) {
        if (lastConsumeUs != 0) {
            const uint32_t gap = nowUs - lastConsumeUs;
            if (gap > gapMaxUs) gapMaxUs = gap;
        }
        lastConsumeUs = nowUs;
        ++framesProduced;
    }

    void resetWindow() { gapMaxUs = 0; }
};
