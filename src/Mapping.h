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

// Signed pixel deflection from centre for a sample at the given vscale.
// Positive = higher input voltage.  Not clamped.
//
// Overflow: max |adc - ADC_MID| = 512, GridDiv = 30, FULL_SCALE_MV = 10000
//   → max numerator = 512 * 30 * 10000 = 153,600,000, well within int32.
static inline int32_t deflectionPx(uint16_t adc, uint16_t vscale_mv_per_div) {
    if (vscale_mv_per_div == 0) return 0;   // guard divide-by-zero (never expected)
    const int32_t delta_counts = (int32_t)adc - ADC_MID;
    return (delta_counts * (int32_t)Theme::GridDiv * FULL_SCALE_MV)
           / (ADC_MID * (int32_t)vscale_mv_per_div);
}

// Map a sample to a Y coordinate (higher voltage → smaller Y = up on screen),
// clamped to the plot's vertical bounds.
static inline int16_t sampleToY(uint16_t adc, uint16_t vscale_mv_per_div) {
    int32_t y = (int32_t)Theme::PlotCY - deflectionPx(adc, vscale_mv_per_div);
    const int32_t lo = Theme::PlotY;
    const int32_t hi = Theme::PlotY + Theme::PlotH - 1;
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    return (int16_t)y;
}

// Map a sample to an X coordinate (higher voltage → larger X = right), clamped
// to the plot's horizontal bounds.  Used by XY mode's horizontal axis.
static inline int16_t sampleToX(uint16_t adc, uint16_t vscale_mv_per_div) {
    int32_t x = (int32_t)Theme::PlotCX + deflectionPx(adc, vscale_mv_per_div);
    const int32_t lo = Theme::PlotX;
    const int32_t hi = Theme::PlotX + Theme::PlotW - 1;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return (int16_t)x;
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
