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
//   B2 (Channel) — toggle the focused channel's trace on/off
//   B3 (RunStop) — arm single-shot: run until the next successful triggered
//                  capture, then freeze and disarm
//   Encoder      — reset all state to defaults
//
// Draw Z-order (bottom → top):
//   1. Background clear (r.clear())
//   2. Waveform grid + traces (activeMode->render())
//   3. HUD text overlay (drawn here, after render())
//
// Acquisition: when state.running, capture() is called once per draw() call
// (i.e. once per frame).  Capture is blocking; at short timebases the sweep
// completes in a few milliseconds.

#include "RunScreen.h"
#include "../Theme.h"
#include "../ScopeState.h"
#include "../Parameter.h"

#include <Arduino.h>   // snprintf on Teensy

// Channel index (0 or 1) that single-channel operations act on.  For Both,
// the conventional lead channel (A = 0) is used, matching fmtVScale's display.
static uint8_t focusedChannel(ChannelSel sel) {
    return (sel == ChannelSel::B) ? 1 : 0;
}

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
// handleEvent
// --------------------------------------------------------------------------
void RunScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
    auto& s = ctx.state;

    if (e.type == EventType::ShortPress) {
        switch (e.button) {
            // B1: advance acquisition mode, then fix selection if invalidated.
            case Btn::Mode:
                s.mode = (Mode)(((int)s.mode + 1) % (int)Mode::COUNT);
                clampSelectable(s);
                break;

            // B2: advance channel selection (three values: A, B, Both).
            case Btn::Channel:
                s.channel = (ChannelSel)(((int)s.channel + 1) % 3);
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
    // Mode long-press (Settings) handled in a later milestone.
}

// --------------------------------------------------------------------------
// draw
// --------------------------------------------------------------------------
void RunScreen::draw(Renderer& r, AppContext& ctx) {
    auto& s = ctx.state;

    // 1. Clear background.
    r.clear();

    // 2. Capture a new sweep when running.
    if (s.running) {
        const bool triggered = _acq.capture(s, _buf);
        // Single-shot: freeze on the first successful triggered capture.
        if (s.singleArmed && triggered) {
            s.running = false;
            s.singleArmed = false;
        }
    }
    // When stopped, _buf retains the last captured sweep — frozen display.

    // 3. Delegate waveform rendering to the active mode strategy.
    ScopeMode* activeMode = _modes[static_cast<int>(s.mode)];
    if (activeMode != nullptr) {
        activeMode->render(r, s, _buf);
    }
    // If activeMode is null (Rolling/XY not yet implemented), waveform area
    // shows only the grid-less background — safe, no crash.

    // 4. HUD overlay — drawn last so it appears on top of the waveform.

    // Mode label centred near the top (within the safe inset band).
    r.text(Theme::RunModeX, Theme::RunModeY, modeName(s.mode), Theme::Text, Theme::HudTitleSize);

    // Selected encoder parameter: name on the existing row.
    r.text(Theme::RunSelLabelX, Theme::RunSelY, "Sel:", Theme::Dim, Theme::HudTextSize);
    r.text(Theme::RunSelValueX, Theme::RunSelY, parameterFor(s.selected).name, Theme::Highlight, Theme::HudTextSize);

    // Active channel.
    char b[24];
    snprintf(b, sizeof b, "Chan: %s", channelName(s.channel));
    r.text(Theme::RunChanX, Theme::RunChanY, b, Theme::Text, Theme::HudTextSize);

    // Run / stop indicator: "ARM" (yellow) while a single-shot is pending,
    // else green "RUN" when running or yellow "STOP" when frozen.
    const char* runLabel = s.singleArmed ? "ARM" : (s.running ? "RUN" : "STOP");
    const uint16_t runColor = (s.running && !s.singleArmed) ? Theme::TraceA
                                                            : Theme::Highlight;
    r.text(Theme::RunStopX, Theme::RunStopY, runLabel, runColor, Theme::HudTextSize);

    // Live formatted value for the selected parameter.
    char val[24];
    parameterFor(s.selected).format(s, val, sizeof val);
    r.text(Theme::RunParamValX, Theme::RunParamValY, val, Theme::Highlight, Theme::HudTextSize);
}
