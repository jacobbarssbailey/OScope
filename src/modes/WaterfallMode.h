// modes/WaterfallMode.h — Scrolling spectrogram (spectrum waterfall).
//
// The time-domain complement to Spectrum: instead of drawing the current
// magnitude spectrum as bars, it draws each FFT frame as a horizontal line of
// colour (frequency on X, magnitude → brightness of the channel's trace colour)
// and scrolls those lines over time, so you see how the spectrum evolves.
//
// Dual channel: A fills the top half, B the bottom half, split by a centre line.
// The newest line for each channel appears next to the centre and older lines
// scroll outward toward the edges, mirroring Spectrum's centre-out layout.
// Frequency spans 0–8192 Hz across the full width (matching Spectrum's view).
//
// Free-running like Spectrum: it reads its own 256-sample block from the rings
// via Acquisition::readNewestBlock() and shares Spectrum's fixed ~32 kHz rate.
// The FFT + a new history line are produced in onFrame(); render() paints the
// whole history each frame through a precomputed heat colour LUT.
#pragma once

#include "ScopeMode.h"
#include <arm_math.h>

class Acquisition;

class WaterfallMode : public ScopeMode {
public:
    WaterfallMode();

    const char* name() const override { return "WFAL"; }

    void setSource(Acquisition* acq) { _src = acq; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
    void onFrame(const SampleBuffers& buf) override;

private:
    static constexpr uint16_t kFFT  = 256;          // FFT length
    static constexpr uint16_t kNBin = kFFT / 2;     // usable bins (1..128)
    static constexpr uint16_t kW    = 240;          // columns (screen width)
    static constexpr uint16_t kRows = 119;          // history lines per channel

    static constexpr int32_t kFminHz = 0;           // frequency window (matches Spectrum)
    static constexpr int32_t kFmaxHz = 8192;
    static constexpr float   kFull    = 16384.0f;   // magnitude at full intensity
    static constexpr float   kFloorDb = -48.0f;     // dB at zero intensity

    Acquisition* _src = nullptr;
    arm_rfft_fast_instance_f32 _fft;
    float    _win[kFFT];
    float    _in[kFFT];
    float    _out[kFFT];
    uint16_t _rawA[kFFT];
    uint16_t _rawB[kFFT];
    float    _mag[kNBin];       // per-bin magnitudes (reused per channel)

    // Per-channel intensity history (0..255), one row per FFT frame.  A's newest
    // row is kRows-1 (next to centre); B's newest is row 0.
    uint8_t  _histA[kRows][kW];
    uint8_t  _histB[kRows][kW];

    // Intensity -> RGB565: a linear ramp from the background to each channel's
    // trace colour (A green, B cyan), matching the oscilloscope traces.
    uint16_t _lutA[256];
    uint16_t _lutB[256];
    uint8_t  _col2bin[kW];      // screen column -> FFT bin index (0..kNBin-1)

    void computeMag(const uint16_t* src, float* mag);
    uint8_t magToIntensity(float mag) const;
    // Scroll a channel's history and insert the newest FFT line.
    void pushRow(const float* mag, uint8_t (*hist)[kW], bool towardTop);
};
