// modes/SpectrumMode.cpp — FFT spectrum analyzer implementation.

#include "SpectrumMode.h"
#include "../Theme.h"
#include "../Acquisition.h"
#include "../Mapping.h"   // FRAC_ONE, for lineAA's Q8 endpoints
#include <math.h>

// Full-scale reference note: a full-scale sinusoid (±512 counts) through a Hann
// window into a 256-point FFT produces a bin magnitude of roughly
// (amplitude * N/2 * coherent gain) = 512 * 128 * 0.25 ≈ 16384, which is why
// kFull is 16384.

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
    _binHz = fs / (float)kFFT;
}

int SpectrumMode::ampToPx(float mag) const {
    // Linear fraction across [zero, full].
    float span = kFull - kZero;
    if (span < 1.0f) span = 1.0f;
    const float lin = (mag - kZero) / span;

    // Log fraction across the same endpoints (needs positive references).
    float frac;
    if (kScalePct == 0) {
        frac = lin;
    } else {
        const float zc = kZero < 1.0f ? 1.0f : kZero;
        const float denom = log10f(kFull / zc);
        const float lg = (mag <= zc || denom <= 0.0f)
                         ? 0.0f : (log10f(mag / zc) / denom);
        const float s = (float)kScalePct / 100.0f;
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

const uint16_t SpectrumMode::kBinSteps[3] = {32, 64, 128};

uint16_t SpectrumMode::bins() const { return kBinSteps[_binStep]; }

int16_t SpectrumMode::bucketW() const {
    return (int16_t)(Theme::SpecBarsW / (int16_t)bins());
}

void SpectrumMode::encoderPress() {
    _layout = (Layout)(((int)_layout + 1) % (int)Layout::COUNT);
}

void SpectrumMode::encoderTurn(int8_t delta) {
    if (delta == 0) return;
    const int last = (int)(sizeof kBinSteps / sizeof kBinSteps[0]) - 1;
    int v = (int)_binStep + (delta > 0 ? 1 : -1);
    if (v < 0) v = 0;
    if (v > last) v = last;      // steps, not wraps: the ends are meaningful
    _binStep = (uint8_t)v;
}

void SpectrumMode::mapBars(const float* mag, uint8_t* bars) const {
    // Each display bucket spans an equal slice of [minHz, maxHz].  Averaging the
    // bins it covers is what makes a low bucket count a genuinely coarser view
    // rather than a decimated one — with 32 buckets each averages four bins, so
    // nothing between them is simply dropped.
    //
    // Where the window is zoomed in far enough that a bucket falls between two
    // bin centres it covers none, and the nearest bin is used instead; that is
    // the only behaviour the original nearest-map had, and it is still right
    // when buckets are finer than bins.
    const uint16_t n     = bins();
    const float    lo    = (float)kMinHz;
    const float    width = (float)(kMaxHz - kMinHz);
    for (uint16_t i = 0; i < n; ++i) {
        const float fLo = lo + (float)i * width / (float)n;
        const float fHi = lo + (float)(i + 1) * width / (float)n;

        int binLo = (int)ceilf(fLo / _binHz);          // first bin centre in range
        int binHi = (int)floorf(fHi / _binHz);         // last bin centre in range
        if (binLo < 1) binLo = 1;
        if (binHi > (int)kNBin) binHi = (int)kNBin;

        float m;
        if (binHi >= binLo) {
            float sum = 0.0f;
            for (int b = binLo; b <= binHi; ++b) sum += mag[b - 1];
            m = sum / (float)(binHi - binLo + 1);
        } else {
            const float freq = (fLo + fHi) * 0.5f;
            int bin = (int)(freq / _binHz + 0.5f);
            if (bin < 1) bin = 1;
            if (bin > (int)kNBin) bin = (int)kNBin;
            m = mag[bin - 1];
        }
        bars[i] = (uint8_t)ampToPx(m);
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

void SpectrumMode::drawGrid(Renderer& r) const {
    if (_layout == Layout::Bars) {
        for (int16_t col = 1; col < Theme::GridCols; ++col) {
            r.vline(Theme::PlotX + col * Theme::GridDiv, Theme::PlotY,
                    Theme::PlotH, Theme::Grid);
        }
        r.hline(Theme::PlotX, Theme::SpecCenterY, Theme::PlotW, Theme::Grid);
        return;
    }
    if (_layout == Layout::Mirror) {
        // The Bars grid a quarter turn round, so the axes still read.
        for (int16_t row = 1; row < Theme::GridRows; ++row) {
            r.hline(Theme::PlotX, Theme::PlotY + row * Theme::GridDiv,
                    Theme::PlotW, Theme::Grid);
        }
        r.vline(Theme::CX, Theme::PlotY, Theme::PlotH, Theme::Grid);
        return;
    }
    // Radial: the split between the two halves, plus two amplitude rings.  The
    // rings are where the spokes start and stop, so they read as a scale.
    const int16_t span = Theme::SpecRadOuter - Theme::SpecRadInner;
    r.ring((int16_t)(Theme::SpecRadInner + span * Theme::SpecRadRing1 / 100), 1,
           Theme::Grid);
    r.ring((int16_t)(Theme::SpecRadInner + span * Theme::SpecRadRing2 / 100), 1,
           Theme::Grid);
    r.ring(Theme::SpecRadOuter, 1, Theme::Grid);
    r.vline(Theme::CX, Theme::PlotY, Theme::PlotH, Theme::Grid);
}

void SpectrumMode::renderBars(Renderer& r, const ScopeState& s) const {
    // Channel A grows up from the centre line, channel B down (inverted).  Bars
    // stop one row short of the centre so the horizontal grid line stays visible.
    //
    // Only the outer end is capped: the end at the centre line stays square so
    // the two channels meet the baseline flat.  At 1 px wide there is no corner
    // to round and barRounded falls through to a plain fill.
    const uint16_t n = bins();
    const int16_t  w = bucketW();
    const int16_t  rad = (int16_t)(w / 2);
    for (uint16_t i = 0; i < n; ++i) {
        const int16_t x = Theme::SpecLeftX + (int16_t)i * w;
        if (s.channelEnabled[0] && _barsA[i] > 0) {
            r.barRounded(x, (int16_t)(Theme::SpecCenterY - _barsA[i]),
                         w, _barsA[i], rad, Theme::TraceA, Renderer::Cap::Top);
        }
        if (s.channelEnabled[1] && _barsB[i] > 0) {
            r.barRounded(x, (int16_t)(Theme::SpecCenterY + 1),
                         w, _barsB[i], rad, Theme::TraceB, Renderer::Cap::Bottom);
        }
    }
}

void SpectrumMode::renderMirror(Renderer& r, const ScopeState& s) const {
    // The same block turned a quarter: frequency runs down the face, A grows
    // left of the centre line and B right.  The round face is widest across the
    // middle, which is where the low buckets — usually the tallest — sit.
    const uint16_t n = bins();
    const int16_t  hgt = bucketW();          // rows per bucket, same arithmetic
    const int16_t  rad = (int16_t)(hgt / 2);
    const int16_t  top = (int16_t)(Theme::CY - Theme::SpecBarsW / 2);
    for (uint16_t i = 0; i < n; ++i) {
        const int16_t y = (int16_t)(top + (int16_t)i * hgt);
        if (s.channelEnabled[0] && _barsA[i] > 0) {
            r.barRounded((int16_t)(Theme::CX - _barsA[i]), y,
                         _barsA[i], hgt, rad, Theme::TraceA, Renderer::Cap::Left);
        }
        if (s.channelEnabled[1] && _barsB[i] > 0) {
            r.barRounded((int16_t)(Theme::CX + 1), y,
                         _barsB[i], hgt, rad, Theme::TraceB, Renderer::Cap::Right);
        }
    }
}

void SpectrumMode::radialChannel(Renderer& r, const uint8_t* bars, int side,
                                 bool outward, uint16_t color) const {
    const uint16_t n    = bins();
    const int16_t  rIn  = Theme::SpecRadInner;
    const int16_t  rOut = Theme::SpecRadOuter;
    const int16_t  span = (int16_t)(rOut - rIn);

    // Each bucket owns a wedge of the half circle.  Filling a wedge as a polygon
    // would need a scanline fill; drawing it as a fan of antialiased spokes
    // spaced under a pixel apart costs about the same and keeps the radial
    // texture, which is the point of the layout.  So the spoke count follows arc
    // length rather than the bucket count — and it is taken at each bar's own
    // outer radius, not the rim, which is most of the cost of a short bar near
    // the hub in the outward layout.
    const float wedge = (float)M_PI / (float)n;

    for (uint16_t i = 0; i < n; ++i) {
        const int mag = bars[i];
        if (mag <= 0) continue;
        const int16_t len = (int16_t)((int32_t)mag * span / Theme::SpecMaxPx);
        if (len <= 0) continue;
        const int16_t r0 = outward ? rIn : (int16_t)(rOut - len);
        const int16_t r1 = outward ? (int16_t)(rIn + len) : rOut;

        int spokes = (int)(wedge * (float)r1 * 0.9f);   // 0.9 leaves a hair of gap
        if (spokes < 1) spokes = 1;

        for (int k = 0; k < spokes; ++k) {
            // Low frequency at the top of both halves, sweeping down each side.
            const float t = ((float)i + ((float)k + 0.5f) / (float)spokes) * wedge;
            const float sx = (float)side * sinf(t);
            const float sy = -cosf(t);
            r.lineAA((int32_t)((Theme::CX + sx * r0) * Mapping::FRAC_ONE),
                     (int32_t)((Theme::CY + sy * r0) * Mapping::FRAC_ONE),
                     (int32_t)((Theme::CX + sx * r1) * Mapping::FRAC_ONE),
                     (int32_t)((Theme::CY + sy * r1) * Mapping::FRAC_ONE),
                     color);
        }
    }
}

void SpectrumMode::renderRadial(Renderer& r, const ScopeState& s,
                                bool outward) const {
    if (s.channelEnabled[0]) radialChannel(r, _barsA, -1, outward, Theme::TraceA);
    if (s.channelEnabled[1]) radialChannel(r, _barsB, +1, outward, Theme::TraceB);
}

void SpectrumMode::render(Renderer& r, const ScopeState& state,
                          const SampleBuffers& /*buf*/) {
    drawGrid(r);
    switch (_layout) {
        case Layout::Mirror:    renderMirror(r, state);        break;
        case Layout::RadialOut: renderRadial(r, state, true);  break;
        case Layout::RadialIn:  renderRadial(r, state, false); break;
        default:                renderBars(r, state);          break;
    }
    // HUD drawn by RunScreen on top afterward.
}
