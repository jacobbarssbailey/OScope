// screens/RunScreen.h — The main oscilloscope run/stop screen.
//
// RunScreen is the root screen for normal operation.  It:
//   - Handles all button/encoder events and mutates ScopeState accordingly.
//   - Calls acquisition.capture() each frame when state.running.
//   - Delegates waveform drawing to the active ScopeMode strategy.
//   - Draws the HUD overlay on top of the waveform so it remains readable.
//
// Holds a static Acquisition instance and SampleBuffers plus one ScopeMode
// instance per Mode enum value; the _modes[] table dispatches drawing to the
// active mode.  All three modes (Triggered, Rolling, XY) are populated.
#pragma once

#include "Screen.h"
#include "../Acquisition.h"
#include "../modes/ScopeMode.h"
#include "../modes/TriggeredMode.h"
#include "../modes/RollingMode.h"
#include "../modes/XYMode.h"

class RunScreen : public Screen {
public:
    RunScreen();

    // Called once when RunScreen becomes the top screen.
    // Initialises the ADC (analogReadResolution) via Acquisition::begin().
    void onEnter(AppContext& ctx) override;

    // Mutate ctx.state in response to button presses / encoder events.
    void handleEvent(const InputEvent& e, AppContext& ctx) override;

    // Capture (if running) then render waveform + HUD into the framebuffer.
    void draw(Renderer& r, AppContext& ctx) override;

private:
    Acquisition   _acq;
    SampleBuffers _buf;
    TriggeredMode _triggeredMode;
    RollingMode   _rollingMode;
    XYMode        _xyMode;

    // Mode dispatch table: indexed by (int)Mode enum value.  All slots populated.
    ScopeMode*    _modes[static_cast<int>(Mode::COUNT)];
};
