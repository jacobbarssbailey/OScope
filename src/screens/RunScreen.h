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

    // Advance acquisition (when running); returns true when a new frame is
    // ready.  Also handles per-frame work (rolling ingest, single-shot).
    bool tick(AppContext& ctx) override;

    // Render the last acquired frame + HUD into the framebuffer.
    void draw(Renderer& r, AppContext& ctx) override;

    // Wire the settings menu opened by B1 (Mode) long-press.
    void setMenuScreen(Screen* menu) { _menu = menu; }

private:
    // Debounced persistence of ScopeState: an event that changes the setup marks
    // it dirty, and tick() saves once the change has settled (batches a burst of
    // encoder detents into a single EEPROM write).
    bool     _stateDirty  = false;
    uint32_t _lastChangeMs = 0;

    // Mode-change flash: the large mode label is shown centered for a short time
    // after a mode change, then hidden so it doesn't obscure the waveform.
    bool     _modeFlash   = false;
    uint32_t _modeFlashMs = 0;

    Screen*       _menu = nullptr;
    Acquisition   _acq;
    TriggeredMode _triggeredMode;
    RollingMode   _rollingMode;
    XYMode        _xyMode;

    // Mode dispatch table: indexed by (int)Mode enum value.  All slots populated.
    ScopeMode*    _modes[static_cast<int>(Mode::COUNT)];
};
