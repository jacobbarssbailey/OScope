// modes/XYMode.cpp — X-Y (Lissajous) mode implementation.
//
// For each sample index i, the point (sampleToX(A[i]), sampleToY(B[i])) is a
// position of the trace.  A drives the horizontal axis, B the vertical; both
// share the same deflection mapping, so a 1:1 signal traces a diagonal and a
// 90°-phase pair traces a circle/ellipse.
//
// Consecutive points are connected with lines (like an analog X-Y beam moving
// continuously) so slow/simple signals fill in rather than showing sparse dots.
// Acquisition always samples both channels in XY mode, so no channel-enable
// guard is needed here.

#include "XYMode.h"
#include "../Mapping.h"
#include "../Theme.h"

void XYMode::render(Renderer& r, const ScopeState& state,
                    const SampleBuffers& buf) {
    Mapping::drawGrid(r);

    const uint16_t n = buf.count;
    if (n < 2) return;  // need at least two points to draw a segment

    const uint16_t vsx = state.vscale_mv_per_div[0];  // A → X
    const uint16_t vsy = state.vscale_mv_per_div[1];  // B → Y

    int16_t x0 = Mapping::sampleToX(buf.ch[0][0], vsx);
    int16_t y0 = Mapping::sampleToY(buf.ch[1][0], vsy);
    for (uint16_t i = 1; i < n; ++i) {
        const int16_t x1 = Mapping::sampleToX(buf.ch[0][i], vsx);
        const int16_t y1 = Mapping::sampleToY(buf.ch[1][i], vsy);
        r.line(x0, y0, x1, y1, Theme::TraceA);
        x0 = x1;
        y0 = y1;
    }
    // HUD drawn by RunScreen on top afterward.
}
