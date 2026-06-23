// screens/RunScreen.cpp — Implementation of the main oscilloscope run screen.
//
// Button mapping (short press):
//   B1 (Mode)    — cycle acquisition mode: TRIG → ROLL → X-Y → TRIG …
//                  then clampSelectable() to fix selection if now invalid.
//   B2 (Channel) — cycle channel: A → B → A+B → A …
//   B3 (RunStop) — toggle run/stop
//   Encoder      — advance selected parameter (skips N/A params for current mode)
//   Encoder long — reset all state to defaults
//   Encoder turn — adjust the selected parameter value by encoder delta

#include "RunScreen.h"
#include "../Theme.h"
#include "../ScopeState.h"
#include "../Parameter.h"

#include <Arduino.h>   // snprintf on Teensy

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

            // B3: toggle run/stop.
            case Btn::RunStop:
                s.running = !s.running;
                break;

            // Encoder press: advance to next selectable parameter, skipping
            // any that do not apply in the current mode.
            case Btn::Encoder:
                s.selected = nextSelectable(s);
                break;

            default:
                break;
        }
    } else if (e.type == EventType::LongPress && e.button == Btn::Encoder) {
        // Encoder long-press: reset everything to factory defaults, then
        // clamp selection in case the defaults land on an invalid parameter.
        s.resetToDefaults();
        clampSelectable(s);
    } else if (e.type == EventType::EncoderTurn) {
        // Encoder rotation: adjust the currently selected parameter.
        parameterFor(s.selected).adjust(s, e.delta);
    }
    // Mode long-press (Settings) and Channel/RunStop long-press:
    // handled in later milestones — intentionally ignored here.
}

void RunScreen::draw(Renderer& r, AppContext& ctx) {
    auto& s = ctx.state;

    r.clear();

    // Mode label centred near the top (within the safe inset band).
    r.text(Theme::RunModeX, Theme::RunModeY, modeName(s.mode), Theme::Text, 2);

    // Selected encoder parameter: name on the existing row.
    r.text(Theme::RunSelLabelX, Theme::RunSelY, "Sel:", Theme::Dim);
    r.text(Theme::RunSelValueX, Theme::RunSelY, parameterFor(s.selected).name, Theme::Highlight);

    // Active channel.
    char b[24];
    snprintf(b, sizeof b, "Chan: %s", channelName(s.channel));
    r.text(Theme::RunChanX, Theme::RunChanY, b, Theme::Text);

    // Run / stop indicator: green when running, yellow when stopped.
    r.text(Theme::RunStopX, Theme::RunStopY, s.running ? "RUN" : "STOP",
           s.running ? Theme::TraceA : Theme::Highlight);

    // Live formatted value for the selected parameter.
    // The parameter name is already shown in the "Sel:" row above.
    char val[24];
    parameterFor(s.selected).format(s, val, sizeof val);
    r.text(Theme::RunParamValX, Theme::RunParamValY, val, Theme::Highlight);
}
