// modes/XYMode.h — X-Y (Lissajous) mode: Channel A vs Channel B.
//
// XYMode implements the ScopeMode strategy for Mode::XY.  It plots the sample
// pairs (x = A, y = B) connected by lines, so two related signals trace a
// Lissajous figure.  V/div scales both axes (A via vscale[0], B via vscale[1]).
//
// X-Y is inherently a two-channel mode; Acquisition always samples both
// channels in XY mode (ignoring the per-channel display-enable flags), so this
// mode can plot unconditionally.
#pragma once

#include "ScopeMode.h"

class XYMode : public ScopeMode {
public:
    const char* name() const override { return "X-Y"; }

    // A fresh Lissajous figure per frame, so a trail accumulates into something
    // meaningful — B2 toggles it here.
    bool honoursPersistence() const override { return true; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
};
