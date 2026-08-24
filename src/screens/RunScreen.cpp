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
//   2. Grid underlay (drawn here, shared by the scope modes)
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
#include <string.h>    // strncpy

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
    _modes[static_cast<int>(Mode::Spectrum)]  = &_spectrumMode;
    _modes[static_cast<int>(Mode::Tuner)]     = &_tunerMode;
    _modes[static_cast<int>(Mode::Waterfall)] = &_waterfallMode;
}

// --------------------------------------------------------------------------
// onEnter: initialise ADC resolution once (called from ScreenStack::reset)
// --------------------------------------------------------------------------
void RunScreen::onEnter(AppContext& ctx) {
    _acq.begin();
    _spectrumMode.setSource(&_acq);   // Spectrum reads its FFT block from the rings
    _tunerMode.setSource(&_acq);      // Tuner reads its YIN block from the rings
    _tunerMode.setSettings(&ctx.settings);   // for the A4 note reference
    _waterfallMode.setSource(&_acq);  // Waterfall reads its FFT block from the rings
}

// --------------------------------------------------------------------------
// Transient parameter band
// --------------------------------------------------------------------------
bool RunScreen::bandActive() const {
    return _band && (millis() - _bandMs) < Theme::BandHoldMs;
}

void RunScreen::showBand(const char* label, const char* value, const char* unit) {
    if (label == nullptr || label[0] == '\0' || value == nullptr || value[0] == '\0') {
        _band = false;
        return;
    }
    strncpy(_bandLabel, label, sizeof _bandLabel - 1);
    strncpy(_bandValue, value, sizeof _bandValue - 1);
    strncpy(_bandUnit, unit ? unit : "", sizeof _bandUnit - 1);
    _bandLabel[sizeof _bandLabel - 1] = '\0';
    _bandValue[sizeof _bandValue - 1] = '\0';
    _bandUnit[sizeof _bandUnit - 1]   = '\0';
    _band      = true;
    _bandMs    = millis();
    _modeFlash = false;   // the band occupies the same space as the mode label
}

void RunScreen::showSelectedParam(const ScopeState& s) {
    if (!paramAppliesInMode(s.selected, s.mode)) { _band = false; return; }
    const Parameter& p = parameterFor(s.selected);
    char val[12], unit[10];
    p.format(s, val, sizeof val, unit, sizeof unit);
    showBand(p.name, val, unit);
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

    // Keep requesting redraws while the mode flash or the parameter band is up
    // so each can time out on its own even when stopped/idle (draw() clears them
    // when they expire).
    const bool flashActive = (_modeFlash && (millis() - _modeFlashMs) < kModeFlashMs)
                             || _band;

    if (!s.running) return flashActive;   // frozen: hold last frame, but honor flash

    // Rolling, XY, Spectrum, Tuner, and Waterfall are free-running: they
    // republish/read the newest window whenever samples arrive.  Triggered still
    // uses update()'s trigger-aligned, sweep-paced path.  Keeping the paths
    // separate leaves the hardened triggered acquisition untouched.
    const bool freeRunning = (s.mode == Mode::Rolling || s.mode == Mode::XY ||
                              s.mode == Mode::Spectrum || s.mode == Mode::Tuner ||
                              s.mode == Mode::Waterfall);
    const bool newFrame = freeRunning
                          ? _acq.updateFreeRunning(s, ctx.settings)
                          : _acq.update(s, ctx.settings);
    _acq.reportDiag(s);
    if (newFrame) {
        // Note the frame; the mode folds it in draw() rather than here.  The
        // per-frame analysis (Tuner's YIN, Spectrum's and Waterfall's FFT) is
        // only ever consumed by the next render, and it is not cheap — YIN is
        // 8 ms.  Doing it per published frame used to be safe only because the
        // loop blocked on the blit and so published at the display rate; once
        // the transfer went asynchronous the publish rate rose 3.4x and YIN
        // alone took 99% of the CPU.  Fold per rendered frame instead.
        _framePending = true;

        // Single-shot: freeze on the first successful triggered capture.  Stays
        // here — it is about the capture happening, not about drawing it.
        if (s.singleArmed && _acq.lastTriggered()) {
            s.running = false;
            s.singleArmed = false;
        }
    }
    return _framePending || flashActive;
}

// --------------------------------------------------------------------------
// handleEvent
// --------------------------------------------------------------------------
void RunScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
    auto& s = ctx.state;
    ScopeMode* activeMode = _modes[static_cast<int>(s.mode)];

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
            // possible future per-channel view).  Modes that repurpose the
            // button (Waterfall's flow direction) take it instead, and flash
            // whatever readout they return.  B2 long-press (enable toggle)
            // still works either way.
            case Btn::Channel:
                if (activeMode && activeMode->ownsChannelButton()) {
                    char label[12] = {0}, value[12] = {0};
                    activeMode->channelPress(label, sizeof label, value, sizeof value);
                    showBand(label, value, "");
                }
                break;

            // B3: toggle run/stop.  A manual run/stop overrides any pending
            // single-shot arm.
            case Btn::RunStop:
                s.running = !s.running;
                s.singleArmed = false;
                break;

            // Encoder press: cycle the active mode's own parameters if it has
            // them (Tuner), otherwise advance to the next shared parameter that
            // applies in the current mode and flash it in the band.
            case Btn::Encoder:
                if (activeMode && activeMode->ownsEncoder()) {
                    activeMode->encoderPress();
                } else {
                    s.selected = nextSelectable(s);
                    showSelectedParam(s);
                }
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
        // Encoder rotation: adjust the active mode's own selected parameter
        // (Tuner), otherwise the shared parameter — unless none applies.  The
        // new value goes up in the band, which re-times itself on every detent.
        if (activeMode && activeMode->ownsEncoder()) {
            activeMode->encoderTurn(e.delta);
        } else if (paramAppliesInMode(s.selected, s.mode)) {
            parameterFor(s.selected).adjust(s, e.delta);
            showSelectedParam(s);
        }
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

    // 1. Clear background — or, in a persistence-capable scope mode with
    //    persistence enabled, fade the previous frame instead so the trace
    //    leaves a phosphor-like trail.  Only Triggered and X-Y qualify: each
    //    draws a fresh snapshot per frame, so accumulation reveals jitter and
    //    modulation.  Rolling already encodes time in its scroll, and the
    //    non-scope modes (Spectrum) manage their own display.
    const bool persistMode = (s.mode == Mode::Triggered || s.mode == Mode::XY);
    if (ctx.settings.persist != 0 && persistMode) {
        // keep/256 per channel per frame, indexed by the persist level.
        static const uint16_t kKeep[4] = {256, 150, 200, 232};
        r.fadeFrame(kKeep[ctx.settings.persist <= 3 ? ctx.settings.persist : 0]);
    } else {
        r.clear();
    }

    // Acquisition runs in tick(); draw renders the last completed frame.  When
    // stopped, _acq.frame() keeps returning that frame — a frozen display.

    // 2. Draw the grid underlay, shared by the scope modes.  Spectrum, Tuner,
    //    and Waterfall draw their own layout inside render(), so skip the shared
    //    8×8 grid for them.
    if (s.mode != Mode::Spectrum && s.mode != Mode::Tuner &&
        s.mode != Mode::Waterfall) {
        Mapping::drawGrid(r);
    }

    // 3. Delegate waveform rendering to the active mode strategy.  Any sweep
    //    acquired since the last render is folded in first, once, on the
    //    freshest published frame — see tick().
    ScopeMode* activeMode = _modes[static_cast<int>(s.mode)];
    if (activeMode != nullptr) {
        if (_framePending) {
            activeMode->onFrame(_acq.frame());
            _framePending = false;
        }
        activeMode->render(r, s, _acq.frame());
    }
    // activeMode is never null (all modes registered); the guard is defensive.

    // 4. Minimal HUD, drawn last (on top of the waveform).

    // Large mode label, centered, for a moment after a mode change.
    if (_modeFlash) {
        if (millis() - _modeFlashMs < kModeFlashMs) {
            r.textCenterX(Theme::ModeY, modeName(s.mode), Theme::Text, FONT_LARGE);
        } else {
            _modeFlash = false;
        }
    }

    // Parameter band, last of all: it masks the waveform behind it, so nothing
    // else may draw over it.  It clears itself once the hold time expires.
    if (_band) {
        if (bandActive()) {
            r.fillRect(Theme::PlotX, (int16_t)(Theme::BandTopY + 1), Theme::PlotW,
                       (int16_t)(Theme::BandBotY - Theme::BandTopY - 1),
                       Theme::Background);
            r.hline(Theme::PlotX, Theme::BandTopY, Theme::PlotW, Theme::Dim);
            r.hline(Theme::PlotX, Theme::BandBotY, Theme::PlotW, Theme::Dim);
            r.textCenterX(Theme::BandLabelY, _bandLabel, Theme::Text, FONT_SMALL);
            r.textUnitCenterX(Theme::BandValueY, _bandValue, _bandUnit, Theme::Text,
                              FONT_LARGE, FONT_BODY);
        } else {
            _band = false;
        }
    }

    // Run state, outermost of all so nothing (not even the band) masks it: a
    // ring around the bezel, solid when frozen and dashed while a single-shot
    // is armed.  Nothing is drawn while running normally.
    if (s.singleArmed) {
        r.ring(Theme::RunRingR, Theme::RunRingW, Theme::Stopped,
               Theme::RunRingDashes);
    } else if (!s.running) {
        r.ring(Theme::RunRingR, Theme::RunRingW, Theme::Stopped);
    }
}
