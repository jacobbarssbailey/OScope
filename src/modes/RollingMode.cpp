// modes/RollingMode.cpp — Free-running scrolling waveform implementation.
//
// Acquisition::updateRolling() publishes the newest N-sample window every frame,
// ungated by the CAPTURE accumulator that paces Triggered/XY.  Consecutive
// windows overlap by (N − samples acquired since the last frame), so plotting
// the window left→right shifts every point left by that many pixels with the
// new samples entering at the right — a continuous right-to-left scroll whose
// frame rate is bounded by the display blit, not the sample rate.  That is what
// holds the rate steady at long timebases (was 6 fps at 10 ms/div).

#include "RollingMode.h"
#include "../Mapping.h"
#include "../Theme.h"

void RollingMode::drawChannel(Renderer& r, const SampleBuffers& buf, uint8_t ch,
                              uint16_t vscale, uint16_t color) const {
    // Vertices are Q8: one sample per column, so X is whole and only Y carries
    // a fraction — which is precisely what lineAA blends with.
    int32_t y0 = Mapping::sampleToYQ8(buf.ch[ch][0], vscale);
    for (uint16_t i = 1; i < buf.count; ++i) {
        const int32_t x0 = (int32_t)(Theme::PlotX + i - 1) << Mapping::FRAC;
        const int32_t x1 = (int32_t)(Theme::PlotX + i) << Mapping::FRAC;
        const int32_t y1 = Mapping::sampleToYQ8(buf.ch[ch][i], vscale);
        r.lineAA(x0, y0, x1, y1, color);
        y0 = y1;
    }
}

void RollingMode::render(Renderer& r, const ScopeState& state,
                         const SampleBuffers& buf) {
    // Grid is drawn by RunScreen before this call.
    if (buf.count < 2) return;

    // Draw B first so A lands on top (matches Triggered mode's Z-order).
    if (state.channelEnabled[1]) {
        drawChannel(r, buf, 1, state.vscale_mv_per_div[1], Theme::TraceB);
    }
    if (state.channelEnabled[0]) {
        drawChannel(r, buf, 0, state.vscale_mv_per_div[0], Theme::TraceA);
    }
    // HUD drawn by RunScreen on top afterward.
}
