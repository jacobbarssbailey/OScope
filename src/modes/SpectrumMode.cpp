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

    buildFan();   // for the starting bucket count; encoderTurn rebuilds it
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

const uint16_t SpectrumMode::kBinSteps[4] = {16, 32, 64, 128};

// off, then fast / medium / slow.  At ~32 fps: fast holds ~0.2 s then clears a
// full-height peak in ~0.8 s; medium holds ~0.75 s and takes ~2.5 s; slow holds
// ~2 s and takes ~7.5 s, which is long enough to catch something you missed.
const SpectrumMode::PeakDecay SpectrumMode::kDecays[4] = {
    { 0,  0, 0},   // off
    { 6,  3, 1},   // fast
    {24,  1, 1},   // medium
    {64,  1, 3},   // slow
};

uint16_t SpectrumMode::bins() const { return kBinSteps[_binStep]; }

int16_t SpectrumMode::bucketW() const {
    return (int16_t)(Theme::SpecBarsW / (int16_t)bins());
}

int16_t SpectrumMode::barW() const {
    const int16_t w = (int16_t)(bucketW() - kBarGap);
    return (w > 0) ? w : 1;   // at 1 px per bucket there is no room for a gap
}

void SpectrumMode::channelPress() {
    _layout = (Layout)(((int)_layout + 1) % (int)Layout::COUNT);
}

void SpectrumMode::channelHold() {
    _decay = (uint8_t)((_decay + 1) % (int)(sizeof kDecays / sizeof kDecays[0]));
    if (kDecays[_decay].hold == 0) {          // switched off: drop what was held
        for (uint16_t i = 0; i < kMaxBins; ++i) {
            _peakA[i] = _peakB[i] = 0;
            _holdA[i] = _holdB[i] = 0;
        }
    }
}

void SpectrumMode::encoderTurn(int8_t delta) {
    if (delta == 0) return;
    const int last = (int)(sizeof kBinSteps / sizeof kBinSteps[0]) - 1;
    int v = (int)_binStep + (delta > 0 ? 1 : -1);
    if (v < 0) v = 0;
    if (v > last) v = last;      // steps, not wraps: the ends are meaningful
    if ((uint8_t)v == _binStep) return;
    _binStep = (uint8_t)v;
    // The buckets now cover different frequencies, so every held peak is about
    // a band that no longer exists at that index.  Drop them rather than let
    // them decay from a value that never applied here.
    for (uint16_t i = 0; i < kMaxBins; ++i) {
        _peakA[i] = _peakB[i] = 0;
        _holdA[i] = _holdB[i] = 0;
    }
    buildFan();
}

void SpectrumMode::agePeaks(const uint8_t* bars, uint8_t* peak,
                            uint8_t* hold) const {
    const PeakDecay& d = kDecays[_decay];
    if (d.hold == 0) return;               // off: nothing to age
    // Rates slower than a pixel a frame skip frames rather than carry a
    // fraction, so a peak is always a whole number of pixels.
    const bool falls = (d.div <= 1) || (_decayPhase % d.div) == 0;
    const uint16_t n = bins();
    for (uint16_t i = 0; i < n; ++i) {
        if (bars[i] >= peak[i]) {          // a new maximum resets the hold
            peak[i] = bars[i];
            hold[i] = d.hold;
        } else if (hold[i] > 0) {
            --hold[i];
        } else if (peak[i] > 0 && falls) {
            peak[i] = (peak[i] > d.step) ? (uint8_t)(peak[i] - d.step) : 0;
        }
    }
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
    // onFrame runs once per *rendered* frame, which is what the decay is
    // measured in — see the ScopeMode::onFrame contract.
    agePeaks(_barsA, _peakA, _holdA);
    agePeaks(_barsB, _peakB, _holdB);
    ++_decayPhase;
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
    const uint16_t n     = bins();
    const int16_t  pitch = bucketW();
    const int16_t  w     = barW();
    const int16_t  rad   = (int16_t)(w / 2);
    const int16_t  cy    = Theme::SpecCenterY;

    for (uint16_t i = 0; i < n; ++i) {
        const int16_t x = Theme::SpecLeftX + (int16_t)i * pitch;

        {
            if (_barsA[i] > 0) {
                r.barRounded(x, (int16_t)(cy - _barsA[i]), w, _barsA[i], rad,
                             Theme::TraceA, Renderer::Cap::Top);
            }
            // The marker floats clear of the bar; the black gap between them is
            // what separates the two, so both can be the channel's own colour.
            if (_peakA[i] >= _barsA[i] + kPeakGapPx) {
                r.fillRect(x, (int16_t)(cy - _peakA[i]), w, kPeakThickPx,
                           Theme::TraceA);
            }
        }
        {
            if (_barsB[i] > 0) {
                r.barRounded(x, (int16_t)(cy + 1), w, _barsB[i], rad,
                             Theme::TraceB, Renderer::Cap::Bottom);
            }
            if (_peakB[i] >= _barsB[i] + kPeakGapPx) {
                r.fillRect(x, (int16_t)(cy + 1 + _peakB[i] - kPeakThickPx), w,
                           kPeakThickPx, Theme::TraceB);
            }
        }
    }
}

// Rebuilt only when the bucket count changes — the fan is a function of that
// and nothing else, so render() never has to touch it and stays the pure
// function of (state, buffers) the ScopeMode contract asks for.
void SpectrumMode::buildFan() {
    const uint16_t n = bins();
    const float wedgeHalf = (float)M_PI / (float)n * 0.5f;
    // Resolution is set by the outer radius, where a wedge's arc is longest.
    // Inner rings reuse the same offsets and so oversample, which costs a little
    // overdraw and nothing else: pixels are written, not blended.
    int steps = (int)(wedgeHalf * 2.0f * (float)Theme::SpecRadOuter / kArcStepPx) + 1;
    if (steps > kMaxFanSteps) steps = kMaxFanSteps;
    _fanSteps = steps;

    for (int k = 0; k < steps; ++k) {
        const float off = (((float)k + 0.5f) / (float)steps - 0.5f) * 2.0f * wedgeHalf;
        _fanOff[k] = off;
        _fanCos[k] = cosf(off);
        _fanSin[k] = sinf(off);
    }
}

void SpectrumMode::radialChannel(Renderer& r, const uint8_t* bars,
                                 const uint8_t* peak, int side, bool outward,
                                 uint16_t color) const {
    uint16_t* fb = r.tft.getFrameBuffer();
    if (fb == nullptr) return;                 // nothing to rasterise into

    const uint16_t n    = bins();
    const int16_t  rIn  = Theme::SpecRadInner;
    const int16_t  rOut = Theme::SpecRadOuter;
    const int16_t  span = (int16_t)(rOut - rIn);
    const float wedge     = (float)M_PI / (float)n;
    const float wedgeHalf = wedge * 0.5f;

    for (uint16_t i = 0; i < n; ++i) {
        // The wedge's centre direction, once per bucket; the fan offsets are
        // rotated onto it with a multiply rather than another sin/cos each.
        const float tc = ((float)i + 0.5f) * wedge;
        const float S  = sinf(tc), C = cosf(tc);

        for (int pass = 0; pass < 2; ++pass) {
            const int mag = pass ? peak[i] : bars[i];
            if (mag <= 0) continue;
            // Off leaves every peak at 0, so the gap test below covers it.
            if (pass && peak[i] < bars[i] + kPeakGapPx) continue;

            const int16_t len = (int16_t)((int32_t)mag * span / Theme::SpecMaxPx);
            if (len <= 0) continue;
            const int16_t tip = outward ? (int16_t)(rIn + len)
                                        : (int16_t)(rOut - len);
            int16_t ra, rb;                     // the radial slice to ink
            if (!pass) {
                ra = outward ? rIn : tip;
                rb = outward ? tip : rOut;
            } else if (outward) {
                rb = (int16_t)(tip + kPeakThickPx);
                if (rb > rOut) rb = rOut;
                ra = (int16_t)(rb - kPeakThickPx);
            } else {
                ra = (int16_t)(tip - kPeakThickPx);
                if (ra < rIn) ra = rIn;
                rb = (int16_t)(ra + kPeakThickPx);
            }
            if (rb <= ra) continue;

            for (int16_t rr = ra; rr <= rb; ++rr) {
                const float halfArc = wedgeHalf * (float)rr;   // half the slice, in px
                // A pixel-measured gap, held to a sane share of the slice.
                float gap = kGapPx;
                const float lo = kGapMinFrac * 2.0f * halfArc;
                const float hi = kGapMaxFrac * 2.0f * halfArc;
                if (gap < lo) gap = lo;
                if (gap > hi) gap = hi;
                float ink = halfArc - gap * 0.5f;              // half the inked arc
                // Near the hub a fine wedge is under a pixel of arc, and no gap
                // fits inside it: taking one anyway breaks the ring into aliased
                // dots.  Ink at least kMinInkPx and let neighbours meet instead.
                if (ink * 2.0f < kMinInkPx) {
                    ink = (halfArc < kMinInkPx * 0.5f) ? halfArc : kMinInkPx * 0.5f;
                }

                if (!pass) {
                    // Round the free tip: narrow the arc along a circular
                    // profile over the last kWedgeCapPx of the wedge's length.
                    const float capR = (halfArc < kWedgeCapPx) ? halfArc : kWedgeCapPx;
                    const float d = outward ? (float)(rb - rr) : (float)(rr - ra);
                    if (d < capR) {
                        const float e = capR - d;
                        ink -= capR - sqrtf(capR * capR - e * e);
                    }
                }
                if (ink <= 0.0f) continue;
                const float inkAng = ink / (float)rr;          // back to radians

                for (int k = 0; k < _fanSteps; ++k) {
                    if (_fanOff[k] < -inkAng || _fanOff[k] > inkAng) continue;
                    // Rotate the fan offset onto this wedge's centre.
                    const float p =  S * _fanCos[k] + C * _fanSin[k];
                    const float q = -C * _fanCos[k] + S * _fanSin[k];
                    const int x = (int)(Theme::CX + (float)side * p * (float)rr + 0.5f);
                    const int y = (int)(Theme::CY + q * (float)rr + 0.5f);
                    if ((unsigned)x < (unsigned)Theme::W &&
                        (unsigned)y < (unsigned)Theme::H) {
                        fb[y * Theme::W + x] = color;
                    }
                }
            }
        }
    }
}

void SpectrumMode::renderRadial(Renderer& r, const ScopeState& s,
                                bool outward) const {
    radialChannel(r, _barsA, _peakA, -1, outward, Theme::TraceA);
    radialChannel(r, _barsB, _peakB, +1, outward, Theme::TraceB);
}

void SpectrumMode::render(Renderer& r, const ScopeState& state,
                          const SampleBuffers& /*buf*/) {
    drawGrid(r);
    switch (_layout) {
        case Layout::RadialOut: renderRadial(r, state, true);  break;
        case Layout::RadialIn:  renderRadial(r, state, false); break;
        default:                renderBars(r, state);          break;
    }
    // HUD drawn by RunScreen on top afterward.
}
