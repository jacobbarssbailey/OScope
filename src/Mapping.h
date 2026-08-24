// Mapping.h — Shared ADC-sample ↔ screen-coordinate mapping and scope grid.
//
// Every mode maps raw 10-bit ADC counts to display coordinates with the same
// calibration.  Centralising it here keeps the ADC_MID / full-scale constants
// and the deflection formula in one place (previously duplicated in
// TriggeredMode).  Header-only + inline: no extra translation unit, no cost.
//
// Deflection formula (input voltage → pixels from centre):
//   adc_mid   = 512 counts            (= 0 V input)
//   FULL_SCALE_MV = 10000 mV          (±10 V half-scale)
//   delta_px  = (adc - adc_mid) * GridDiv * FULL_SCALE_MV
//               / (adc_mid * vscale_mv_per_div)
// A positive delta means a higher input voltage: smaller Y (up) / larger X (right).
#pragma once

#include "Renderer.h"
#include "Theme.h"
#include <stdint.h>

namespace Mapping {

// Nominal 10-bit ADC mid-scale count = 0 V input.  Hardware-calibrate if the
// measured mid-rail differs (keep in sync with ADC_MID in Acquisition.cpp).
constexpr int32_t ADC_MID = 512;
// Full-scale input voltage in mV (hardware input range = ±10 V half-scale).
constexpr int32_t FULL_SCALE_MV = 10000;

// ---- Sub-pixel coordinates -------------------------------------------------
// Positions are carried in Q8 fixed point (1/256 px).  Rounding a sample to a
// whole pixel here throws away exactly the information an antialiased line
// would blend with: with integer endpoints a shallow trace still renders as a
// flat run followed by a hard step, and antialiasing it changes almost nothing.
// Q8 costs nothing (the divide happens either way) and makes the blend worth
// doing.  FRAC is public so callers can convert without re-deriving the shift.
constexpr int     FRAC     = 8;
constexpr int32_t FRAC_ONE = 1 << FRAC;

// Whole pixels from a Q8 coordinate, rounded to nearest.
static inline int16_t toPx(int32_t q) {
    return (int16_t)((q + FRAC_ONE / 2) >> FRAC);
}

// Signed Q8 pixel deflection from centre for a sample at the given vscale.
// Positive = higher input voltage.  Not clamped.
//
// The scale constant is folded so the Q8 numerator still fits in int32:
//   (counts * GridDiv * FULL_SCALE_MV / ADC_MID) << 8
//     = counts * GridDiv * (10000 * 256 / 512)
//     = counts * GridDiv * 5000
// Overflow: max |adc - ADC_MID| = 512, GridDiv = 30
//   → max numerator = 512 * 30 * 5000 = 76,800,000, well within int32.
static inline int32_t deflectionQ8(uint16_t adc, uint16_t vscale_mv_per_div) {
    if (vscale_mv_per_div == 0) return 0;   // guard divide-by-zero (never expected)
    const int32_t delta_counts = (int32_t)adc - ADC_MID;
    static_assert(FULL_SCALE_MV * FRAC_ONE / ADC_MID == 5000,
                  "folded Q8 scale constant no longer matches FULL_SCALE_MV/ADC_MID");
    return (delta_counts * (int32_t)Theme::GridDiv * 5000)
           / (int32_t)vscale_mv_per_div;
}

// Signed whole-pixel deflection from centre.  Kept for callers that genuinely
// want a pixel; the trace path uses the Q8 form.
static inline int32_t deflectionPx(uint16_t adc, uint16_t vscale_mv_per_div) {
    return deflectionQ8(adc, vscale_mv_per_div) >> FRAC;
}

// Map a sample to a Q8 Y coordinate (higher voltage → smaller Y = up on
// screen), clamped to the plot's vertical bounds.
static inline int32_t sampleToYQ8(uint16_t adc, uint16_t vscale_mv_per_div) {
    int32_t y = ((int32_t)Theme::PlotCY << FRAC) - deflectionQ8(adc, vscale_mv_per_div);
    const int32_t lo = (int32_t)Theme::PlotY << FRAC;
    const int32_t hi = (int32_t)(Theme::PlotY + Theme::PlotH - 1) << FRAC;
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    return y;
}

// Map a sample to a Q8 X coordinate (higher voltage → larger X = right),
// clamped to the plot's horizontal bounds.  Used by XY mode's horizontal axis.
static inline int32_t sampleToXQ8(uint16_t adc, uint16_t vscale_mv_per_div) {
    int32_t x = ((int32_t)Theme::PlotCX << FRAC) + deflectionQ8(adc, vscale_mv_per_div);
    const int32_t lo = (int32_t)Theme::PlotX << FRAC;
    const int32_t hi = (int32_t)(Theme::PlotX + Theme::PlotW - 1) << FRAC;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return x;
}

// Whole-pixel forms, for callers that want a pixel rather than a trace vertex.
static inline int16_t sampleToY(uint16_t adc, uint16_t vscale_mv_per_div) {
    return toPx(sampleToYQ8(adc, vscale_mv_per_div));
}

static inline int16_t sampleToX(uint16_t adc, uint16_t vscale_mv_per_div) {
    return toPx(sampleToXQ8(adc, vscale_mv_per_div));
}

// Draw the oscilloscope grid: interior lines at GridDiv spacing.
static inline void drawGrid(Renderer& r) {
    for (int16_t col = 1; col < Theme::GridCols; ++col) {
        const int16_t x = Theme::PlotX + col * Theme::GridDiv;
        r.vline(x, Theme::PlotY, Theme::PlotH, Theme::Grid);
    }
    for (int16_t row = 1; row < Theme::GridRows; ++row) {
        const int16_t y = Theme::PlotY + row * Theme::GridDiv;
        r.hline(Theme::PlotX, y, Theme::PlotW, Theme::Grid);
    }
}

}  // namespace Mapping
