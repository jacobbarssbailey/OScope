// modes/TriggeredMode.cpp — Triggered waveform mode implementation.
//
// sampleToY formula (from header):
//   y = PlotCY - (adc - ADC_MID) * GridDiv * FULL_SCALE_MV
//                / (ADC_MID * vscale_mv_per_div)
//
//   Where:
//     ADC_MID       = 512   (10-bit mid-scale = 0 V input)
//     FULL_SCALE_MV = 10000 (mV at full ADC deflection, i.e. ±10 V input range)
//     GridDiv       = Theme::GridDiv = 30 px/div
//     PlotCY        = Theme::PlotCY  = 120 px (screen centre = 0 V)
//
//   Numerically (using int32 throughout to avoid overflow):
//     delta = (int32)(adc - ADC_MID) * GridDiv * FULL_SCALE_MV
//     y     = PlotCY - delta / (ADC_MID * vscale_mv_per_div)
//   Clamped to [PlotY, PlotY + PlotH - 1].
//
// Z-order:
//   1. RunScreen calls r.clear() — background filled.
//   2. TriggeredMode::render() draws grid, then traces.
//   3. RunScreen draws the HUD text on top — HUD remains readable.
//   (RunScreen::draw() calls mode->render() before the HUD text rows.)

#include "TriggeredMode.h"
#include "../Theme.h"

// Nominal 10-bit ADC mid-scale count = 0 V input.
static constexpr int32_t ADC_MID       = 512;
// Full-scale input voltage in mV (hardware input range = ±10 V = 10000 mV half-scale).
static constexpr int32_t FULL_SCALE_MV = 10000;

// --------------------------------------------------------------------------
// sampleToY — static helper
// --------------------------------------------------------------------------
int16_t TriggeredMode::sampleToY(uint16_t adc, uint16_t vscale_mv_per_div) {
    // Guard against divide-by-zero (should not happen; vscale is always > 0).
    if (vscale_mv_per_div == 0) return Theme::PlotCY;

    // Signed deflection in ADC counts from mid-rail.
    const int32_t delta_counts = (int32_t)adc - ADC_MID;

    // Convert to pixel deflection.
    // Positive delta_counts = above mid-rail = higher input voltage = smaller Y (up on screen).
    // Numerator: delta_counts * GridDiv * FULL_SCALE_MV
    // Denominator: ADC_MID * vscale_mv_per_div
    // Both fit in int32: max |delta_counts| = 512, GridDiv = 30, FULL_SCALE_MV = 10000
    //   → max numerator = 512 * 30 * 10000 = 153,600,000 — fits in int32 (max ~2.1e9).
    const int32_t delta_px = (delta_counts * (int32_t)Theme::GridDiv * FULL_SCALE_MV)
                             / (ADC_MID * (int32_t)vscale_mv_per_div);

    // Y decreases upward on screen.
    int32_t y = (int32_t)Theme::PlotCY - delta_px;

    // Clamp to plot area bounds.
    const int32_t yMin = (int32_t)Theme::PlotY;
    const int32_t yMax = (int32_t)(Theme::PlotY + Theme::PlotH - 1);
    if (y < yMin) y = yMin;
    if (y > yMax) y = yMax;

    return (int16_t)y;
}

// --------------------------------------------------------------------------
// drawGrid — horizontal and vertical grid lines at GridDiv spacing
// --------------------------------------------------------------------------
void TriggeredMode::drawGrid(Renderer& r) const {
    // Vertical grid lines (constant X, spanning full plot height).
    for (int16_t col = 1; col < Theme::GridCols; ++col) {
        const int16_t x = Theme::PlotX + col * Theme::GridDiv;
        r.vline(x, Theme::PlotY, Theme::PlotH, Theme::Grid);
    }

    // Horizontal grid lines (constant Y, spanning full plot width).
    for (int16_t row = 1; row < Theme::GridRows; ++row) {
        const int16_t y = Theme::PlotY + row * Theme::GridDiv;
        r.hline(Theme::PlotX, y, Theme::PlotW, Theme::Grid);
    }

    // Centre crosshair (slightly brighter via a second draw pass is optional;
    // for now we just rely on the grid lines crossing at (120, 120)).
}

// --------------------------------------------------------------------------
// render — draw grid then traces
// --------------------------------------------------------------------------
void TriggeredMode::render(Renderer& r, const ScopeState& state,
                           const SampleBuffers& buf) {
    // 1. Draw the background grid.
    drawGrid(r);

    // 2. Plot each enabled channel as a connected polyline.
    //    We draw channel B first so A (the trigger source) is on top.
    const uint16_t nSamples = buf.count;
    if (nSamples < 2) return;  // nothing to draw

    // Channel B (index 1) — drawn first (below A).
    if (state.channelEnabled[1]) {
        const uint16_t vscale = state.vscale_mv_per_div[1];
        int16_t y0 = sampleToY(buf.ch[1][0], vscale);
        for (uint16_t i = 1; i < nSamples; ++i) {
            const int16_t x0 = (int16_t)(Theme::PlotX + i - 1);
            const int16_t x1 = (int16_t)(Theme::PlotX + i);
            const int16_t y1 = sampleToY(buf.ch[1][i], vscale);
            r.tft.drawLine(x0, y0, x1, y1, Theme::TraceB);
            y0 = y1;
        }
    }

    // Channel A (index 0) — drawn second (on top of B).
    if (state.channelEnabled[0]) {
        const uint16_t vscale = state.vscale_mv_per_div[0];
        int16_t y0 = sampleToY(buf.ch[0][0], vscale);
        for (uint16_t i = 1; i < nSamples; ++i) {
            const int16_t x0 = (int16_t)(Theme::PlotX + i - 1);
            const int16_t x1 = (int16_t)(Theme::PlotX + i);
            const int16_t y1 = sampleToY(buf.ch[0][i], vscale);
            r.tft.drawLine(x0, y0, x1, y1, Theme::TraceA);
            y0 = y1;
        }
    }
    // HUD is drawn by RunScreen after this returns, appearing on top.
}
