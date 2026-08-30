// modes/SpectrumMode.h — FFT spectrum analyzer mode.
//
// Runs a 256-point real FFT (ARM CMSIS-DSP, hardware FP on the Cortex-M7) on the
// newest samples of each channel and draws a magnitude spectrum: channel A grows
// up from the centre line, channel B grows down (inverted), over vertical
// division grid lines and one centre horizontal line.
//
// The encoder rotation sets how many buckets those bars are divided into.  The
// block keeps its width whatever the count, so the bars get wider as they get
// fewer, and the counts are exactly the divisors of SpecBarsW that leave a
// bucket no wider than kMaxBucketW: 128 x 1 px, 64 x 2 px, 32 x 4 px.  Fewer
// buckets average more FFT bins together, which is the coarser view.
//
// The display window and amplitude mapping are fixed (bench-tuned defaults, no
// runtime controls):
//   Fmin/Fmax : the frequency window the buckets span (a zoom over the FFT
//               bins).  A bucket covering several bins averages them; where the
//               window is zoomed in far enough that a bucket falls inside one
//               bin, it takes the nearest.
//   Scale     : 0 % = linear amplitude, 100 % = log; blended in between.
//   Zero/Full : the magnitudes mapped to 0 and full bar height.
//
// Free-running like Rolling/XY, but a 256-point FFT needs more samples than the
// N-sample display frame carries, so it reads its own block straight from the
// rings via Acquisition::readNewestBlock() (source wired by RunScreen).  The
// sample rate is fixed (see kSpectrumTimebaseUs): ~32 kHz → ~126 Hz per bin.
//
// The FFT + magnitude and the bucket mapping run once per frame in onFrame()
// (cached into the bucket arrays); render() only draws, per the ScopeMode
// contract.
#pragma once

#include "ScopeMode.h"
#include <arm_math.h>

class Acquisition;

class SpectrumMode : public ScopeMode {
public:
    SpectrumMode();

    const char* name() const override { return "SPEC"; }

    // Draws each channel only when its enable flag is set, so the Channel
    // button means something here.
    bool honoursChannelEnable() const override { return true; }

    // The encoder rotation walks the bucket count and the press walks the
    // layout; this mode has none of the shared settings for either to drive.
    bool ownsEncoderTurn() const override { return true; }
    void encoderTurn(int8_t delta) override;
    bool ownsEncoderPress() const override { return true; }
    void encoderPress() override;

    // Wire the capture source the FFT block is read from (called by RunScreen).
    void setSource(Acquisition* acq) { _src = acq; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
    void onFrame(const SampleBuffers& buf) override;

    // How the buckets are arranged on the face.  Bars is the classic reading;
    // the rest trade the linear frequency axis for one that uses the round face
    // instead of fighting it.
    enum class Layout : uint8_t {
        Bars,        // A up / B down from a horizontal centre line
        Mirror,      // the same block a quarter turn round: A left / B right
        RadialOut,   // spokes from a hub outward, A left half / B right half
        RadialIn,    // spokes from the rim inward, same halves
        COUNT
    };

private:
    static constexpr uint16_t kFFT  = 256;                 // FFT length
    static constexpr uint16_t kNBin = kFFT / 2;            // usable bins (1..128)
    static constexpr uint16_t kMaxBins    = 128;   // widest count = array size
    static constexpr int16_t  kMaxBucketW = 4;     // widest a single bucket gets

    // Selectable bucket counts, coarse to fine.  These are every divisor of
    // Theme::SpecBarsW (128) whose quotient is <= kMaxBucketW, which is what
    // keeps the block exactly as wide at every setting with uniform bars.
    static const uint16_t kBinSteps[3];
    uint8_t  _binStep = 2;      // index into kBinSteps; starts at the finest
    Layout   _layout  = Layout::Bars;

    // Fixed display parameters (bench-tuned defaults).
    static constexpr int32_t kMinHz    = 0;        // low edge of the window
    static constexpr int32_t kMaxHz    = 8192;     // high edge of the window
    static constexpr uint8_t kScalePct = 20;       // 0 = linear, 100 = log
    static constexpr float   kZero     = 0.0f;     // magnitude at 0 px
    static constexpr float   kFull     = 16384.0f; // magnitude at full height

    float _binHz = 126.0f;     // FFT bin width in Hz (set in the constructor)

    Acquisition* _src = nullptr;
    arm_rfft_fast_instance_f32 _fft;
    float    _win[kFFT];        // Hann window coefficients
    float    _in[kFFT];         // windowed input (also FFT scratch; clobbered)
    float    _out[kFFT];        // FFT output (CMSIS packed real/imag)
    uint16_t _rawA[kFFT];       // newest raw samples read from the rings
    uint16_t _rawB[kFFT];
    float    _magA[kNBin];      // per-bin magnitudes (bins 1..128)
    float    _magB[kNBin];
    uint8_t  _barsA[kMaxBins] = {0};  // bucket heights in px (0..SpecMaxPx)
    uint8_t  _barsB[kMaxBins] = {0};

    // Active bucket count and the width each one draws at.  Their product is
    // always Theme::SpecBarsW.
    uint16_t bins() const;
    int16_t  bucketW() const;

    // Window + FFT one channel's kFFT samples into per-bin magnitudes.
    void computeMag(const uint16_t* src, float* mag);
    // Map per-bin magnitudes onto the active display buckets using the
    // frequency window and amplitude parameters.
    void mapBars(const float* mag, uint8_t* bars) const;
    // Blended linear/log magnitude -> bar height in px.
    int  ampToPx(float mag) const;

    // One per Layout; each draws its own grid, since they share no axes.
    void drawGrid(Renderer& r) const;
    void renderBars(Renderer& r, const ScopeState& s) const;
    void renderMirror(Renderer& r, const ScopeState& s) const;
    void renderRadial(Renderer& r, const ScopeState& s, bool outward) const;
    // Lay one channel's spokes over a half circle.  `side` is -1 for the left
    // half (A) and +1 for the right (B).
    void radialChannel(Renderer& r, const uint8_t* bars, int side, bool outward,
                       uint16_t color) const;
};
