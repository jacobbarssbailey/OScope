// modes/RollingMode.h — Free-running, right-to-left scrolling waveform mode.
//
// RollingMode implements the ScopeMode strategy for Mode::Rolling.  Unlike
// Triggered mode it does not wait for a trigger; acquisition free-runs and each
// fresh sweep is appended to a persistent history ring.  The ring is rendered
// oldest→newest, left→right, so new data enters at the right edge and older
// data scrolls off the left.
//
// Ring semantics: one slot per horizontal pixel (SampleBuffers::N).  Until the
// ring fills, only the left portion is populated (trace grows rightward); once
// full, the newest sample pins to the right edge.
#pragma once

#include "ScopeMode.h"

class RollingMode : public ScopeMode {
public:
    const char* name() const override { return "ROLL"; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;

    // Append the completed sweep into the history ring (once per new frame).
    void onFrame(const SampleBuffers& buf) override;

private:
    static constexpr uint16_t N = SampleBuffers::N;
    uint16_t _ring[2][N] = {{0}, {0}};  // per-channel history, indexed by write pos
    uint16_t _head  = 0;                // next write position
    uint16_t _count = 0;                // valid samples retained (≤ N)

    // Plot one channel's retained history as a left→right polyline.
    void drawChannel(Renderer& r, uint8_t ch, uint16_t vscale, uint16_t color) const;
};
