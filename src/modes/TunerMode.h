// modes/TunerMode.h — Dual-channel instrument tuner (YIN pitch detection).
//
// Runs the YIN algorithm (AcqCore::yinPeriod) on the newest window of each
// channel and shows the detected pitch: channel A in the top half of the
// display, channel B in the bottom half.  Turning the encoder toggles the
// readout between frequency (Hz) and musical note; in note mode a cents bar
// flanks the centre line showing how sharp/flat each channel is.  The note
// mapping uses the A4 reference frequency from Settings (default 440 Hz).
//
// Free-running like Rolling/XY/Spectrum: it reads its own analysis block from
// the rings via Acquisition::readNewestBlock() (source wired by RunScreen).  The
// sample rate is fixed (kTunerTimebaseUs, ~16 kHz); with a 1024-sample window
// that resolves ~31 Hz upward.  YIN runs once per frame in onFrame(); render()
// only draws.
#pragma once

#include "ScopeMode.h"

class Acquisition;
struct Settings;

class TunerMode : public ScopeMode {
public:
    TunerMode();

    const char* name() const override { return "TUNE"; }

    void setSource(Acquisition* acq)        { _src = acq; }
    void setSettings(const Settings* s)     { _settings = s; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
    void onFrame(const SampleBuffers& buf) override;

    // The encoder press toggles the Hz/Note readout (this mode has no shared
    // settings for the press to walk).
    bool ownsEncoderPress() const override { return true; }
    void encoderPress() override;

    // TODO: B2 is the per-mode option key and Tuner is the one mode with
    // nothing on it.
    //
    // A hold on the last confident reading was tried and removed.  It worked as
    // written, but its trigger — analyze() returning -1 — almost never fires on
    // a Eurorack input: there is always enough noise, residual DC ripple or a
    // still-sounding VCO to clear kMinPP and give YIN *some* period, so the
    // readout shows a live garbage pitch rather than a held good one.  Doing it
    // properly needs a confidence number, not a binary: AcqCore::yinPeriod
    // computes the normalized difference at the chosen lag and then throws it
    // away, so surfacing that is the prerequisite.
    //
    // Cheaper candidates that need nothing new: a fine cents scale (the meter
    // spans +/-50, and the last few cents are where tuning actually happens);
    // a response speed, since smooth() blends at a fixed 0.7/0.3; sharps vs
    // flats in the note names; or transpose, since the menu's A4 only shifts
    // the whole grid.

private:
    static constexpr uint16_t kWin       = 1024;   // YIN window length
    static constexpr float    kYinThresh = 0.15f;  // YIN absolute threshold
    static constexpr uint16_t kMinPP     = 15;     // min peak-to-peak counts to analyze

    Acquisition*    _src      = nullptr;
    const Settings* _settings = nullptr;
    bool  _showNote = true;     // false = Hz readout, true = note readout
    float _fsHz     = 16129.0f; // sample rate (set in the constructor)
    float _freqA    = -1.0f;    // smoothed detected pitch, -1 = none
    float _freqB    = -1.0f;

    uint16_t _rawA[kWin];
    uint16_t _rawB[kWin];
    float    _x[kWin];          // float working copy (per channel, reused)
    float    _diff[kWin / 2 + 1];  // YIN scratch (reused)

    // Analyze one raw block: returns detected frequency in Hz, or -1 if none.
    float analyze(const uint16_t* raw);
    // Fold a new reading into a smoothed estimate (snap on a big jump).
    static float smooth(float prev, float fresh);
    // Draw one channel's readout (big glyph + subscript) and its cents meter.
    void drawChannel(Renderer& r, float freq, uint16_t color,
                     int16_t bigY, int16_t meterY) const;
};
