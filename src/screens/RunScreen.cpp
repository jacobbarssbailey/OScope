// screens/RunScreen.cpp — Implementation of the main oscilloscope run screen.
//
// Button mapping (short press only for this milestone):
//   B1 (Mode)    — cycle acquisition mode: TRIG → ROLL → X-Y → TRIG …
//   B2 (Channel) — cycle channel: A → B → A+B → A …
//   B3 (RunStop) — toggle run/stop
//   Encoder      — cycle selected encoder parameter: Time → V/div → Trig → …
//   Encoder long — reset all state to defaults
//
// Mode long-press (Settings) and Channel/RunStop long-press are left for
// later tasks and produce no action here.

#include "RunScreen.h"
#include "../Theme.h"
#include "../ScopeState.h"

#include <Arduino.h>   // snprintf on Teensy

// Local helper: human-readable encoder-parameter label.
// Task 3 will replace this with parameterFor().name via the Parameter abstraction.
static const char* paramName(EncoderParam p) {
    switch (p) {
        case EncoderParam::Timebase:     return "Time";
        case EncoderParam::VScale:       return "V/div";
        case EncoderParam::TriggerLevel: return "Trig";
        default:                         return "?";
    }
}

void RunScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
    auto& s = ctx.state;

    if (e.type == EventType::ShortPress) {
        switch (e.button) {
            // B1: advance acquisition mode (wraps at COUNT).
            case Btn::Mode:
                s.mode = (Mode)(((int)s.mode + 1) % (int)Mode::COUNT);
                break;

            // B2: advance channel selection (three values: A, B, Both).
            case Btn::Channel:
                s.channel = (ChannelSel)(((int)s.channel + 1) % 3);
                break;

            // B3: toggle run/stop.
            case Btn::RunStop:
                s.running = !s.running;
                break;

            // Encoder press: advance selected parameter.
            // Task 3 will replace this inline cycle with nextSelectable().
            case Btn::Encoder:
                s.selected = (EncoderParam)(((int)s.selected + 1) % 3);
                break;

            default:
                break;
        }
    } else if (e.type == EventType::LongPress && e.button == Btn::Encoder) {
        // Encoder long-press: reset everything to factory defaults.
        s.resetToDefaults();
    }
    // Mode long-press (Settings) and Channel/RunStop long-press:
    // handled in later milestones — intentionally ignored here.
}

void RunScreen::draw(Renderer& r, AppContext& ctx) {
    auto& s = ctx.state;

    r.clear();

    // Mode label centred near the top (within the safe inset band).
    r.text(Theme::RunModeX, Theme::RunModeY, modeName(s.mode), Theme::Text, 2);

    // Selected encoder parameter.
    r.text(Theme::RunSelLabelX, Theme::RunSelY, "Sel:", Theme::Dim);
    r.text(Theme::RunSelValueX, Theme::RunSelY, paramName(s.selected), Theme::Highlight);

    // Active channel.
    char b[24];
    snprintf(b, sizeof b, "Chan: %s", channelName(s.channel));
    r.text(Theme::RunChanX, Theme::RunChanY, b, Theme::Text);

    // Run / stop indicator: green when running, yellow when stopped.
    r.text(Theme::RunStopX, Theme::RunStopY, s.running ? "RUN" : "STOP",
           s.running ? Theme::TraceA : Theme::Highlight);
}
