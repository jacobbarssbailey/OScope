// modes/TriggeredMode.h — Triggered (single-sweep, trigger-aligned) waveform mode.
//
// TriggeredMode implements the ScopeMode strategy for Mode::Triggered.
// It draws the oscilloscope grid and then plots each enabled channel as a
// connected polyline.  Grid and sample→pixel mapping come from Mapping.h
// (Mapping::drawGrid / Mapping::sampleToY) — no pixel literals or formulas here.
#pragma once

#include "ScopeMode.h"

class TriggeredMode : public ScopeMode {
public:
    const char* name() const override { return "TRIG"; }

    // Draw the grid, then each enabled channel's waveform.
    // Z-order: grid first, traces on top of grid, HUD drawn by RunScreen on top of both.
    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
};
