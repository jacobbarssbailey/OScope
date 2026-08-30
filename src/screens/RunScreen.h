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
#include "../modes/SpectrumMode.h"
#include "../modes/TunerMode.h"
#include "../modes/WaterfallMode.h"

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

    // A sweep has been published since the last render.  The active mode folds
    // it in at draw time rather than when it is acquired, so the per-frame
    // analysis runs once per displayed frame instead of once per capture — see
    // the note in tick().
    bool     _framePending = false;

    // Transient settings overlay: acquisition settings are off screen until one
    // is selected or changed, then the whole face dims and every parameter that
    // applies in the current mode is listed for Theme::SettingsHoldMs.  Only the
    // timer is state — the rows themselves are read from ScopeState at draw
    // time, so an edit is on screen the moment it lands.
    bool     _settings      = false;
    uint32_t _settingsMs    = 0;

    // Persistence level to restore when B2 switches it back on.  Captured from
    // Settings each time it is switched off, so the menu's choice of how long a
    // trail lasts survives the toggle.  Not persisted itself: a board that was
    // powered down with persistence off comes back at the default.
    uint8_t  _persistLast = 3;   // "long", matching Settings::defaults()

    // Raise the settings overlay, unless the mode has no adjustable parameters.
    void showSettings(const ScopeState& s);
    // True while the overlay is still within its hold time.
    bool settingsActive() const;
    // Draw the dimmed face and the parameter rows.  Call last — it masks
    // everything under it.
    void drawSettings(Renderer& r, const ScopeState& s);

    Screen*       _menu = nullptr;
    Acquisition   _acq;
    TriggeredMode _triggeredMode;
    RollingMode   _rollingMode;
    XYMode        _xyMode;
    SpectrumMode  _spectrumMode;
    TunerMode     _tunerMode;
    WaterfallMode _waterfallMode;

    // Mode dispatch table: indexed by (int)Mode enum value.  All slots populated.
    ScopeMode*    _modes[static_cast<int>(Mode::COUNT)];
};
