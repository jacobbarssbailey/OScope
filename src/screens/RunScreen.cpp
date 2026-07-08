// screens/RunScreen.cpp — Implementation of the main oscilloscope run screen.
//
// Button mapping (short press):
//   B1 (Mode)    — cycle acquisition mode: TRIG → ROLL → X-Y → TRIG …
//                  then clampSelectable() to fix selection if now invalid.
//   B2 (Channel) — cycle channel: A → B → A+B → A …
//   B3 (RunStop) — toggle run/stop (also disarms a pending single-shot)
//   Encoder      — advance selected parameter (skips N/A params for current mode)
//   Encoder turn — adjust the selected parameter value by encoder delta
// Long press:
//   B1 (Mode)    — open the settings menu (push MenuScreen)
//   B2 (Channel) — toggle the focused channel's trace on/off
//   B3 (RunStop) — arm single-shot: run until the next successful triggered
//                  capture, then freeze and disarm
//   Encoder      — reset all state to defaults
//
// Draw Z-order (bottom → top):
//   1. Background clear (r.clear())
//   2. Grid underlay (drawn here when settings.grid, shared by all modes)
//   3. Waveform traces (activeMode->render())
//   4. HUD text overlay (drawn here, after render())
//
// Acquisition: driven by tick() (called every main-loop iteration), which
// advances the non-blocking Acquisition state machine one sample at a time.
// draw() renders whatever complete frame Acquisition last published, so the
// loop stays responsive even at slow timebases.  When stopped, tick() does
// nothing and the last frame stays frozen on screen.

#include "RunScreen.h"
#include "../Theme.h"
#include "../ScopeState.h"
#include "../Parameter.h"
#include "../Settings.h"
#include "../Mapping.h"
#include "../Fonts.h"

#include <Arduino.h>   // snprintf on Teensy

// How long the large mode label stays up after a mode change.
static constexpr uint32_t kModeFlashMs = 1500;

// Channel index (0 or 1) that single-channel operations act on.  For Both,
// the conventional lead channel (A = 0) is used, matching fmtVScale's display.
static uint8_t focusedChannel(ChannelSel sel) {
    return (sel == ChannelSel::B) ? 1 : 0;
}

// How long the acquisition setup must be unchanged before it is written to
// EEPROM.  Batches a burst of encoder detents into a single write.
static constexpr uint32_t kStateSaveDelayMs = 2000;

// --------------------------------------------------------------------------
// Constructor: initialise mode table and zero buffers
// --------------------------------------------------------------------------
RunScreen::RunScreen() {
    // Populate the mode dispatch table, one entry per Mode enum value.
    for (int i = 0; i < static_cast<int>(Mode::COUNT); ++i) {
        _modes[i] = nullptr;
    }
    _modes[static_cast<int>(Mode::Triggered)] = &_triggeredMode;
    _modes[static_cast<int>(Mode::Rolling)]   = &_rollingMode;
    _modes[static_cast<int>(Mode::XY)]        = &_xyMode;
}

// --------------------------------------------------------------------------
// onEnter: initialise ADC resolution once (called from ScreenStack::reset)
// --------------------------------------------------------------------------
void RunScreen::onEnter(AppContext& /*ctx*/) {
    _acq.begin();
}

// --------------------------------------------------------------------------
// tick — advance non-blocking acquisition (called every loop iteration)
// --------------------------------------------------------------------------
bool RunScreen::tick(AppContext& ctx) {
    auto& s = ctx.state;

    // Debounced persistence: save the acquisition setup once changes have
    // settled.  Runs regardless of run/stop so edits made while stopped persist.
    if (_stateDirty && (millis() - _lastChangeMs) >= kStateSaveDelayMs) {
        s.save();
        _stateDirty = false;
    }

    // Keep requesting redraws while the mode flash is up so it can time out on
    // its own even when stopped/idle (draw() clears _modeFlash when it expires).
    const bool flashActive = _modeFlash && (millis() - _modeFlashMs) < kModeFlashMs;

    if (!s.running) return flashActive;   // frozen: hold last frame, but honor flash

    const bool newFrame = _acq.update(s, ctx.settings);
    if (newFrame) {
        // Let the active mode fold the completed sweep into any history it keeps
        // (Rolling); others no-op.  Done here (once per frame), not in draw().
        ScopeMode* activeMode = _modes[static_cast<int>(s.mode)];
        if (activeMode) activeMode->onFrame(_acq.frame());

        // Single-shot: freeze on the first successful triggered capture.
        if (s.singleArmed && _acq.lastTriggered()) {
            s.running = false;
            s.singleArmed = false;
        }
    }
    return newFrame || flashActive;
}

// --------------------------------------------------------------------------
// handleEvent
// --------------------------------------------------------------------------
void RunScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
    auto& s = ctx.state;

    if (e.type == EventType::ShortPress) {
        switch (e.button) {
            // B1: advance acquisition mode, then fix selection if invalidated.
            // Flash the large mode label briefly to confirm the change.
            case Btn::Mode:
                s.mode = (Mode)(((int)s.mode + 1) % (int)Mode::COUNT);
                clampSelectable(s);
                _modeFlash   = true;
                _modeFlashMs = millis();
                break;

            // B2: channel selection is fixed to A+B in the v2 UI, so short-press
            // channel cycling is disabled (the ChannelSel logic is kept for a
            // possible future per-channel view).  B2 long-press (enable toggle)
            // still works.
            case Btn::Channel:
                break;

            // B3: toggle run/stop.  A manual run/stop overrides any pending
            // single-shot arm.
            case Btn::RunStop:
                s.running = !s.running;
                s.singleArmed = false;
                break;

            // Encoder press: advance to next selectable parameter, skipping
            // any that do not apply in the current mode.
            case Btn::Encoder:
                s.selected = nextSelectable(s);
                break;

            default:
                break;
        }
    } else if (e.type == EventType::LongPress) {
        switch (e.button) {
            // Encoder long-press: reset everything to factory defaults, then
            // clamp selection in case the defaults land on an invalid parameter.
            case Btn::Encoder:
                s.resetToDefaults();
                clampSelectable(s);
                Theme::applyPalette(s.colorScheme);   // reset restores Classic
                break;

            // B1 long-press: open the settings menu.
            case Btn::Mode:
                if (_menu) ctx.screens.push(_menu, ctx);
                break;

            // B2 long-press: toggle the focused channel's trace on/off.  When
            // disabled the trace isn't drawn and its V/div edits are skipped.
            case Btn::Channel: {
                const uint8_t c = focusedChannel(s.channel);
                s.channelEnabled[c] = !s.channelEnabled[c];
                break;
            }

            // B3 long-press: arm single-shot — run until the next successful
            // triggered capture, then freeze (completion handled in draw()).
            case Btn::RunStop:
                s.singleArmed = true;
                s.running = true;
                break;

            default:
                break;
        }
    } else if (e.type == EventType::EncoderTurn) {
        // Encoder rotation: adjust the currently selected parameter.
        parameterFor(s.selected).adjust(s, e.delta);
    }

    // Any handled event may have changed the acquisition setup; mark it for a
    // debounced save.  Harmless when nothing persisted actually changed (e.g.
    // run/stop) — EEPROM put() only rewrites changed bytes.
    _stateDirty   = true;
    _lastChangeMs = millis();
}

// --------------------------------------------------------------------------
// draw
// --------------------------------------------------------------------------
void RunScreen::draw(Renderer& r, AppContext& ctx) {
    auto& s = ctx.state;

    // 1. Clear background.
    r.clear();

    // Acquisition runs in tick(); draw renders the last completed frame.  When
    // stopped, _acq.frame() keeps returning that frame — a frozen display.

    // 2. Draw the grid underlay (shared by all modes) when enabled in settings.
    if (ctx.settings.grid) {
        Mapping::drawGrid(r);
    }

    // 3. Delegate waveform rendering to the active mode strategy.
    ScopeMode* activeMode = _modes[static_cast<int>(s.mode)];
    if (activeMode != nullptr) {
        activeMode->render(r, s, _acq.frame());
    }
    // activeMode is never null (all modes registered); the guard is defensive.

    // 4. Minimal HUD, drawn last (on top of the waveform).

    // Top indicator: "ARM" while a single-shot is pending, "STOP" when frozen.
    // Nothing is shown while running normally.
    if (s.singleArmed) {
        r.textCenterX(Theme::StopY, "ARM", Theme::Highlight, Arial_16);
    } else if (!s.running) {
        r.textCenterX(Theme::StopY, "STOP", Theme::Highlight, Arial_16);
    }

    // Selected parameter readout (timebase / V/div / trigger), centered near the
    // bottom.  No label — the units make it clear what is being adjusted.
    char val[24];
    parameterFor(s.selected).format(s, val, sizeof val);
    r.textCenterX(Theme::ParamY, val, Theme::Highlight, Arial_16);

    // Large mode label, centered, for a moment after a mode change.
    if (_modeFlash) {
        if (millis() - _modeFlashMs < kModeFlashMs) {
            r.textCenterX(Theme::ModeY, modeName(s.mode), Theme::Text, Arial_24);
        } else {
            _modeFlash = false;
        }
    }
}
