// modes/SpectrumMode.h — FFT spectrum analyzer mode.
//
// Runs a 256-point real FFT (ARM CMSIS-DSP, hardware FP on the Cortex-M7) on the
// newest samples of each channel and draws a 128-bucket magnitude spectrum:
// channel A grows up from the centre line, channel B grows down (inverted).
//
// Five live parameters are cycled with the encoder button and adjusted with the
// knob (this mode ownsEncoder()):
//   Fmin / Fmax : the frequency window the 128 buckets span (a zoom over the
//                 FFT bins; buckets nearest-map onto bins).
//   Scale       : 0 % = linear amplitude, 100 % = log; values in between blend.
//   Zero / Full : the magnitudes mapped to 0 and full bar height.
//
// Free-running like Rolling/XY, but a 256-point FFT needs more samples than the
// N-sample display frame carries, so it reads its own block straight from the
// rings via Acquisition::readNewestBlock() (source wired by RunScreen).  The
// sample rate is fixed (see kSpectrumTimebaseUs): ~32 kHz → ~126 Hz per bin.
//
// The FFT + magnitude and the bucket mapping run once per frame in onFrame()
// (cached into the bucket arrays); render() only draws, per the ScopeMode
// contract.  The parameters are RAM-only (reset on reboot).
#pragma once

#include "ScopeMode.h"
#include <arm_math.h>

class Acquisition;

class SpectrumMode : public ScopeMode {
public:
    SpectrumMode();

    const char* name() const override { return "SPEC"; }

    // Wire the capture source the FFT block is read from (called by RunScreen).
    void setSource(Acquisition* acq) { _src = acq; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
    void onFrame(const SampleBuffers& buf) override;

    // Mode-owned encoder parameters (see ScopeMode / RunScreen routing).
    bool ownsEncoder() const override { return true; }
    void encoderPress() override;
    void encoderTurn(int8_t delta) override;
    void formatParam(char* buf, uint8_t n) const override;

private:
    static constexpr uint16_t kFFT  = 256;                 // FFT length
    static constexpr uint16_t kNBin = kFFT / 2;            // usable bins (1..128)
    static constexpr uint16_t kBins = 128;                 // display buckets

    // Adjustable parameters, in cycle order.
    enum Param : uint8_t { PMinFreq, PMaxFreq, PScale, PZero, PFull, PCount };
    uint8_t _sel      = PMinFreq;
    int32_t _minHz    = 0;         // low edge of the displayed window
    int32_t _maxHz    = 8192;      // high edge (clamped to Nyquist)
    uint8_t _scalePct = 20;        // 0 = linear, 100 = log
    float   _zero     = 0.0f;      // magnitude at 0 px
    float   _full     = 16384.0f;  // magnitude at full height

    // Adjust step per encoder detent, and the fixed sample-rate-derived limits.
    static constexpr int32_t kFreqStep = 100;      // Hz
    static constexpr int32_t kZeroStep = 50;       // magnitude units
    static constexpr int32_t kFullStep = 256;
    static constexpr int32_t kFullMax  = 65535;
    static constexpr int32_t kScaleStep = 5;       // percent
    int32_t _nyquistHz = 16000;    // set in the constructor from the sample rate
    float   _binHz     = 126.0f;   // FFT bin width in Hz

    Acquisition* _src = nullptr;
    arm_rfft_fast_instance_f32 _fft;
    float    _win[kFFT];        // Hann window coefficients
    float    _in[kFFT];         // windowed input (also FFT scratch; clobbered)
    float    _out[kFFT];        // FFT output (CMSIS packed real/imag)
    uint16_t _rawA[kFFT];       // newest raw samples read from the rings
    uint16_t _rawB[kFFT];
    float    _magA[kNBin];      // per-bin magnitudes (bins 1..128)
    float    _magB[kNBin];
    uint8_t  _barsA[kBins] = {0};  // bucket heights in px (0..SpecMaxPx)
    uint8_t  _barsB[kBins] = {0};

    // Window + FFT one channel's kFFT samples into per-bin magnitudes.
    void computeMag(const uint16_t* src, float* mag);
    // Map per-bin magnitudes onto the 128 display buckets using the current
    // frequency window and amplitude parameters.
    void mapBars(const float* mag, uint8_t* bars) const;
    // Blended linear/log magnitude -> bar height in px (uses _zero/_full/_scale).
    int  ampToPx(float mag) const;

    // Draw the spectrum grid: vertical division lines + one centre horizontal.
    void drawGrid(Renderer& r) const;
};
