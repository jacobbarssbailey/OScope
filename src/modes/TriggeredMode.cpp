// modes/TriggeredMode.cpp — Triggered waveform mode implementation.
//
// Grid drawing and the ADC-count → pixel mapping live in Mapping.h and are
// shared with the other modes.  Acquisition trigger-aligns the sweep, so here
// we simply plot each enabled channel as a connected polyline.
//
// Z-order:
//   1. RunScreen calls r.clear() — background filled.
//   2. TriggeredMode::render() draws grid, then traces.
//   3. RunScreen draws the HUD text on top — HUD remains readable.

#include "TriggeredMode.h"
#include "../Mapping.h"
#include "../Theme.h"

// --------------------------------------------------------------------------
// render — draw grid then traces
// --------------------------------------------------------------------------
void TriggeredMode::render(Renderer& r, const ScopeState& state,
                           const SampleBuffers& buf) {
    // 1. Draw the background grid.
    Mapping::drawGrid(r);

    // 2. Plot each enabled channel as a connected polyline.
    //    We draw channel B first so A (the trigger source) is on top.
    const uint16_t nSamples = buf.count;
    if (nSamples < 2) return;  // nothing to draw

    // Channel B (index 1) — drawn first (below A).
    if (state.channelEnabled[1]) {
        const uint16_t vscale = state.vscale_mv_per_div[1];
        int16_t y0 = Mapping::sampleToY(buf.ch[1][0], vscale);
        for (uint16_t i = 1; i < nSamples; ++i) {
            const int16_t x0 = (int16_t)(Theme::PlotX + i - 1);
            const int16_t x1 = (int16_t)(Theme::PlotX + i);
            const int16_t y1 = Mapping::sampleToY(buf.ch[1][i], vscale);
            r.line(x0, y0, x1, y1, Theme::TraceB);
            y0 = y1;
        }
    }

    // Channel A (index 0) — drawn second (on top of B).
    if (state.channelEnabled[0]) {
        const uint16_t vscale = state.vscale_mv_per_div[0];
        int16_t y0 = Mapping::sampleToY(buf.ch[0][0], vscale);
        for (uint16_t i = 1; i < nSamples; ++i) {
            const int16_t x0 = (int16_t)(Theme::PlotX + i - 1);
            const int16_t x1 = (int16_t)(Theme::PlotX + i);
            const int16_t y1 = Mapping::sampleToY(buf.ch[0][i], vscale);
            r.line(x0, y0, x1, y1, Theme::TraceA);
            y0 = y1;
        }
    }
    // HUD is drawn by RunScreen after this returns, appearing on top.
}
