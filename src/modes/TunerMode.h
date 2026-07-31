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

    // Encoder rotation toggles the Hz/Note readout (this mode owns the encoder).
    bool ownsEncoder() const override { return true; }
    void encoderTurn(int8_t delta) override;

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
    // Draw one channel's readout in a half of the display.
    void drawChannel(Renderer& r, float freq, uint16_t color,
                     int16_t bigY, int16_t barY) const;
};
