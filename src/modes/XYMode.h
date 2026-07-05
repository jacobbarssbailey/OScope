// modes/XYMode.h — X-Y (Lissajous) mode: Channel A vs Channel B.
//
// XYMode implements the ScopeMode strategy for Mode::XY.  It plots each sample
// pair as a dot at (x = A, y = B), so two related signals trace a Lissajous
// figure.  V/div scales both axes (A via vscale[0], B via vscale[1]).
//
// X-Y is inherently a two-channel mode: both channels must be sampled.  Since
// Acquisition only reads a channel when it is enabled, XYMode shows a hint if
// either channel is disabled rather than plotting stale data.
#pragma once

#include "ScopeMode.h"

class XYMode : public ScopeMode {
public:
    const char* name() const override { return "X-Y"; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
};
