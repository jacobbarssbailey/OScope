// modes/XYMode.cpp — X-Y (Lissajous) mode implementation.
//
// For each sample index i, plot a dot at (sampleToX(A[i]), sampleToY(B[i])).
// A drives the horizontal axis, B the vertical.  Both axes share the same
// deflection mapping, so a 1:1 signal traces a diagonal line and a 90°-phase
// pair traces a circle/ellipse.

#include "XYMode.h"
#include "../Mapping.h"
#include "../Theme.h"

// Draw a 2×2 dot at (x, y), guarded against the right/bottom edge so we never
// write outside the framebuffer.  drawPixel itself clips, but the guard keeps
// the intent explicit.
static void dot(Renderer& r, int16_t x, int16_t y, uint16_t color) {
    r.tft.drawPixel(x, y, color);
    if (x + 1 < Theme::W)                        r.tft.drawPixel(x + 1, y, color);
    if (y + 1 < Theme::H)                        r.tft.drawPixel(x, y + 1, color);
    if (x + 1 < Theme::W && y + 1 < Theme::H)    r.tft.drawPixel(x + 1, y + 1, color);
}

void XYMode::render(Renderer& r, const ScopeState& state,
                    const SampleBuffers& buf) {
    Mapping::drawGrid(r);

    // Both channels must be captured for a meaningful X-Y plot.
    if (!state.channelEnabled[0] || !state.channelEnabled[1]) {
        r.text(Theme::RunChanX, Theme::PlotCY, "X-Y needs A+B", Theme::Dim);
        return;
    }

    const uint16_t n = buf.count;
    const uint16_t vsx = state.vscale_mv_per_div[0];  // A → X
    const uint16_t vsy = state.vscale_mv_per_div[1];  // B → Y
    for (uint16_t i = 0; i < n; ++i) {
        const int16_t x = Mapping::sampleToX(buf.ch[0][i], vsx);
        const int16_t y = Mapping::sampleToY(buf.ch[1][i], vsy);
        dot(r, x, y, Theme::TraceA);
    }
    // HUD drawn by RunScreen on top afterward.
}
