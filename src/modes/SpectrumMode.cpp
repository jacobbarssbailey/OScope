// modes/SpectrumMode.cpp — FFT spectrum analyzer implementation.

#include "SpectrumMode.h"
#include "../Theme.h"
#include "../Acquisition.h"
#include <stdio.h>   // snprintf
#include <math.h>

// Full-scale reference note: a full-scale sinusoid (±512 counts) through a Hann
// window into a 256-point FFT produces a bin magnitude of roughly
// (amplitude * N/2 * coherent gain) = 512 * 128 * 0.25 ≈ 16384, which is why the
// default _full is 16384.  All four amplitude/scale endpoints are live-tunable.

static int32_t clampi32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

SpectrumMode::SpectrumMode() {
    arm_rfft_fast_init_f32(&_fft, kFFT);
    // Hann window: 0.5 - 0.5*cos(2*pi*i/(N-1)).  Precomputed once — cuts the
    // spectral leakage that a rectangular window would smear across buckets.
    for (uint16_t i = 0; i < kFFT; ++i) {
        _win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (kFFT - 1));
    }

    // Derive the fixed sample rate the same way Acquisition does, so the bin/Hz
    // mapping matches the timer: interval = timebase * GridCols / N (integer µs),
    // Fs = 1e6 / interval.  With kSpectrumTimebaseUs this is ~32 kHz.
    uint32_t interval = (kSpectrumTimebaseUs * (uint32_t)Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    const float fs = 1000000.0f / (float)interval;
    _binHz     = fs / (float)kFFT;
    _nyquistHz = (int32_t)(fs / 2.0f);   // upper clamp for the Fmax parameter
}

int SpectrumMode::ampToPx(float mag) const {
    // Linear fraction across [zero, full].
    float span = _full - _zero;
    if (span < 1.0f) span = 1.0f;
    const float lin = (mag - _zero) / span;

    // Log fraction across the same endpoints (needs positive references).
    float frac;
    if (_scalePct == 0) {
        frac = lin;
    } else {
        const float zc = _zero < 1.0f ? 1.0f : _zero;
        const float denom = log10f(_full / zc);
        const float lg = (mag <= zc || denom <= 0.0f)
                         ? 0.0f : (log10f(mag / zc) / denom);
        const float s = (float)_scalePct / 100.0f;
        frac = (1.0f - s) * lin + s * lg;
    }

    if (frac <= 0.0f) return 0;
    if (frac >= 1.0f) return Theme::SpecMaxPx;
    return (int)(frac * Theme::SpecMaxPx + 0.5f);
}

void SpectrumMode::computeMag(const uint16_t* src, float* mag) {
    // Remove DC (the ~512-count mid-rail offset) so it doesn't leak into the low
    // bins, then apply the Hann window.
    float mean = 0.0f;
    for (uint16_t i = 0; i < kFFT; ++i) mean += (float)src[i];
    mean /= (float)kFFT;
    for (uint16_t i = 0; i < kFFT; ++i) {
        _in[i] = ((float)src[i] - mean) * _win[i];
    }

    arm_rfft_fast_f32(&_fft, _in, _out, 0);   // clobbers _in (used as scratch)

    // CMSIS packs the real FFT as: _out[0]=DC real, _out[1]=Nyquist real, then
    // _out[2k], _out[2k+1] = real/imag of bin k for k=1..N/2-1.  mag[j] holds
    // bin j+1, so mag[0..126] = bins 1..127 and mag[127] = Nyquist (bin 128).
    for (uint16_t k = 1; k < kNBin; ++k) {
        const float re = _out[2 * k];
        const float im = _out[2 * k + 1];
        mag[k - 1] = sqrtf(re * re + im * im);
    }
    mag[kNBin - 1] = fabsf(_out[1]);
}

void SpectrumMode::mapBars(const float* mag, uint8_t* bars) const {
    // Each display bucket spans an equal slice of [minHz, maxHz]; its centre
    // frequency picks the nearest FFT bin.  When the window is narrower than the
    // full spectrum this zooms in (several buckets share a bin); wider slices
    // skip bins.
    const float lo = (float)_minHz;
    const float width = (float)(_maxHz - _minHz);
    for (uint16_t i = 0; i < kBins; ++i) {
        const float freq = lo + (i + 0.5f) * width / (float)kBins;
        int bin = (int)(freq / _binHz + 0.5f);   // bin number 1..kNBin
        if (bin < 1) bin = 1;
        if (bin > kNBin) bin = kNBin;
        bars[i] = (uint8_t)ampToPx(mag[bin - 1]);
    }
}

void SpectrumMode::onFrame(const SampleBuffers& /*buf*/) {
    // The display frame is too short for a 256-point FFT; read our own block.
    if (!_src || !_src->readNewestBlock(_rawA, _rawB, kFFT)) return;  // hold last
    computeMag(_rawA, _magA);
    computeMag(_rawB, _magB);
    mapBars(_magA, _barsA);
    mapBars(_magB, _barsB);
}

void SpectrumMode::encoderPress() {
    _sel = (uint8_t)((_sel + 1) % PCount);
}

void SpectrumMode::encoderTurn(int8_t delta) {
    switch (_sel) {
        case PMinFreq:
            _minHz = clampi32(_minHz + delta * kFreqStep, 0, _maxHz - kFreqStep);
            break;
        case PMaxFreq:
            _maxHz = clampi32(_maxHz + delta * kFreqStep, _minHz + kFreqStep, _nyquistHz);
            break;
        case PScale:
            _scalePct = (uint8_t)clampi32(_scalePct + delta * kScaleStep, 0, 100);
            break;
        case PZero:
            _zero = (float)clampi32((int32_t)_zero + delta * kZeroStep,
                                    0, (int32_t)_full - kZeroStep);
            break;
        case PFull:
            _full = (float)clampi32((int32_t)_full + delta * kFullStep,
                                    (int32_t)_zero + kFullStep, kFullMax);
            break;
        default: break;
    }
}

void SpectrumMode::formatParam(char* buf, uint8_t n) const {
    switch (_sel) {
        case PMinFreq: snprintf(buf, n, "Fmin %ld Hz", (long)_minHz); break;
        case PMaxFreq: snprintf(buf, n, "Fmax %ld Hz", (long)_maxHz); break;
        case PScale:   snprintf(buf, n, "Scale %u%%", _scalePct);     break;  // 0=lin 100=log
        case PZero:    snprintf(buf, n, "Zero %ld",   (long)_zero);   break;
        case PFull:    snprintf(buf, n, "Full %ld",   (long)_full);   break;
        default:       if (n) buf[0] = '\0';                          break;
    }
}

void SpectrumMode::drawGrid(Renderer& r) const {
    for (int16_t col = 1; col < Theme::GridCols; ++col) {
        r.vline(Theme::PlotX + col * Theme::GridDiv, Theme::PlotY, Theme::PlotH,
                Theme::Grid);
    }
    r.hline(Theme::PlotX, Theme::SpecCenterY, Theme::PlotW, Theme::Grid);
}

void SpectrumMode::render(Renderer& r, const ScopeState& state,
                          const SampleBuffers& /*buf*/) {
    drawGrid(r);

    // Channel A grows up from the centre line, channel B down (inverted).  Bars
    // stop one row short of the centre so the horizontal grid line stays visible.
    for (uint16_t i = 0; i < kBins; ++i) {
        const int16_t x = Theme::SpecLeftX + (int16_t)i * Theme::SpecBucketW;
        if (state.channelEnabled[0] && _barsA[i] > 0) {
            r.fillRect(x, Theme::SpecCenterY - _barsA[i],
                       Theme::SpecBucketW, _barsA[i], Theme::TraceA);
        }
        if (state.channelEnabled[1] && _barsB[i] > 0) {
            r.fillRect(x, Theme::SpecCenterY + 1,
                       Theme::SpecBucketW, _barsB[i], Theme::TraceB);
        }
    }
    // HUD drawn by RunScreen on top afterward.
}
