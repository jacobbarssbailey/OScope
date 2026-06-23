// modes/TriggeredMode.h — Triggered (single-sweep, trigger-aligned) waveform mode.
//
// TriggeredMode implements the ScopeMode strategy for Mode::Triggered.
// It draws the oscilloscope grid and then plots each enabled channel as a
// connected polyline of sampleToY-mapped points.
//
// The grid and sampleToY mapping use only Theme constants — no pixel literals.
// See TriggeredMode.cpp for the detailed mapping formulas.
#pragma once

#include "ScopeMode.h"

class TriggeredMode : public ScopeMode {
public:
    const char* name() const override { return "TRIG"; }

    // Draw the grid, then each enabled channel's waveform.
    // Z-order: grid first, traces on top of grid, HUD drawn by RunScreen on top of both.
    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;

private:
    // Draw the oscilloscope grid lines (horizontal + vertical at GridDiv spacing).
    void drawGrid(Renderer& r) const;

    // Convert a raw 10-bit ADC sample count to a display Y coordinate.
    //
    // Mapping (documented):
    //   adc_mid   = 512 counts  (= 0 V input, maps to PlotCY = 120 px)
    //   vscale    = vscale_mv_per_div  (mV of input signal per screen division)
    //   mv_per_px = vscale_mv_per_div / GridDiv  (mV per pixel)
    //   delta_mV  = (adc - adc_mid) * 10000 / 512   (input mV, positive = above mid-rail)
    //   delta_px  = delta_mV / mv_per_px  (pixels above centre; positive = up → lower Y)
    //   y         = PlotCY - delta_px
    //
    //   Combined: y = PlotCY - (adc - 512) * GridDiv * 10000 / (512 * vscale_mv_per_div)
    //   Clamped to [PlotY, PlotY+PlotH-1] = [0, 239].
    //
    // Example (vscale=1000 mV/div, GridDiv=30):
    //   1 V input (1000 mV) → (1000/512) ADC counts above mid ≈ 51 counts
    //   → delta_px = 51 * 30 * 10000 / (512 * 1000) = 51 * 300000 / 512000 ≈ 30 px up
    //   → y = 120 - 30 = 90 px  (one division above centre)
    static int16_t sampleToY(uint16_t adc, uint16_t vscale_mv_per_div);
};
