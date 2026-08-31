// modes/RollingMode.h — Free-running, right-to-left scrolling waveform mode.
//
// RollingMode implements the ScopeMode strategy for Mode::Rolling.  Unlike
// Triggered mode it does not wait for a trigger; acquisition free-runs and
// Acquisition::updateRolling() republishes the newest N-sample window each
// frame.  Because consecutive windows overlap, drawing the published window
// left→right reads as a scroll: new data enters at the right edge and older
// data leaves at the left.  No cross-frame history is kept here — the capture
// ring is the history, so there is nothing to accumulate in onFrame().
#pragma once

#include "ScopeMode.h"

class RollingMode : public ScopeMode {
public:
    const char* name() const override { return "ROLL"; }


    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;

private:
    // Plot one channel of the published window as a left→right polyline.
    void drawChannel(Renderer& r, const SampleBuffers& buf, uint8_t ch,
                     uint16_t vscale, uint16_t color) const;
};
