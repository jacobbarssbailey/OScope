// screens/RunScreen.h — The main oscilloscope run/stop screen.
//
// RunScreen is the root screen for normal operation.  It displays the current
// mode, selected encoder parameter, active channel, and run/stop status, and
// wires all four buttons and the encoder to the corresponding ScopeState
// mutations.  Real waveform rendering will be added in Task 4.
#pragma once

#include "Screen.h"

class RunScreen : public Screen {
public:
    // Mutate ctx.state in response to button presses / encoder events.
    void handleEvent(const InputEvent& e, AppContext& ctx) override;

    // Render the status overlay into the framebuffer.
    void draw(Renderer& r, AppContext& ctx) override;
};
