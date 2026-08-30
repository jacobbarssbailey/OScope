// modes/WaterfallMode.cpp — Scrolling spectrogram implementation.

#include "WaterfallMode.h"
#include "../Theme.h"
#include "../Acquisition.h"
#include <string.h>   // memset
#include <stdio.h>    // snprintf
#include <math.h>

// Linear ramp from the (black) background up to `color`, scaled by v/255 per
// RGB565 channel.  Baked once into a 256-entry LUT so render() is a plain table
// lookup per pixel.
static uint16_t rampColor(uint16_t color, int v) {
    const int r = ((color >> 11) & 0x1F) * v / 255;
    const int g = ((color >> 5)  & 0x3F) * v / 255;
    const int b = ( color        & 0x1F) * v / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

WaterfallMode::WaterfallMode() {
    arm_rfft_fast_init_f32(&_fft, kFFT);
    for (uint16_t i = 0; i < kFFT; ++i)
        _win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (kFFT - 1));

    for (int v = 0; v < 256; ++v) {
        _lutA[v] = rampColor(Theme::TraceA, v);   // channel A: background -> pink
        _lutB[v] = rampColor(Theme::TraceB, v);   // channel B: background -> periwinkle
    }

    reshape();
}

void WaterfallMode::reshape() {
    _cells = (_flow == Flow::Up) ? kCellsUp : kCellsOut;
    _lines = (_flow == Flow::Up) ? kLinesUp : kLinesOut;
    _head  = 0;

    // Fixed sample rate (shared with Spectrum): interval = timebase*GridCols/N
    // (integer µs), Fs = 1e6/interval.
    uint32_t interval = (kWaterfallTimebaseUs * (uint32_t)Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    const float fs    = 1000000.0f / (float)interval;
    const float binHz = fs / (float)kFFT;

    // Map each frequency cell to the nearest FFT bin across [kFminHz, kFmaxHz].
    for (uint16_t c = 0; c < _cells; ++c) {
        const float freq = kFminHz + (c + 0.5f) * (kFmaxHz - kFminHz) / (float)_cells;
        int bin = (int)(freq / binHz + 0.5f);
        if (bin < 1) bin = 1;
        if (bin > kNBin) bin = kNBin;
        _cell2bin[c] = (uint8_t)(bin - 1);
    }

    memset(_hist, 0, sizeof _hist);
}

void WaterfallMode::channelPress() {
    _flow = (_flow == Flow::Up) ? Flow::Out : Flow::Up;
    reshape();
}

void WaterfallMode::computeMag(const uint16_t* src, float* mag) {
    float mean = 0.0f;
    for (uint16_t i = 0; i < kFFT; ++i) mean += (float)src[i];
    mean /= (float)kFFT;
    for (uint16_t i = 0; i < kFFT; ++i) _in[i] = ((float)src[i] - mean) * _win[i];

    arm_rfft_fast_f32(&_fft, _in, _out, 0);   // clobbers _in

    for (uint16_t k = 1; k < kNBin; ++k) {
        const float re = _out[2 * k], im = _out[2 * k + 1];
        mag[k - 1] = sqrtf(re * re + im * im);
    }
    mag[kNBin - 1] = fabsf(_out[1]);   // Nyquist
}

uint8_t WaterfallMode::magToIntensity(float mag) const {
    if (mag <= 0.0f) return 0;
    const float db = 20.0f * log10f(mag / kFull);
    float frac = (db - kFloorDb) / (0.0f - kFloorDb);
    if (frac <= 0.0f) return 0;
    if (frac >= 1.0f) return 255;
    return (uint8_t)(frac * 255.0f);
}

void WaterfallMode::pushLine(const float* mag, uint8_t* hist) {
    // Precompute the intensity of each bin once, then fan out to the cells.
    uint8_t intBin[kNBin];
    for (uint16_t k = 0; k < kNBin; ++k) intBin[k] = magToIntensity(mag[k]);

    uint8_t* line = hist + (size_t)_head * _cells;
    for (uint16_t c = 0; c < _cells; ++c) line[c] = intBin[_cell2bin[c]];
}

void WaterfallMode::onFrame(const SampleBuffers& /*buf*/) {
    if (!_src || !_src->readNewestBlock(_rawA, _rawB, kFFT)) return;  // hold last

    // Advance the ring once per frame; both channels share the time axis, so
    // they write the same slot.
    _head = (uint16_t)((_head + 1) % _lines);
    computeMag(_rawA, _mag);
    pushLine(_mag, _hist[0]);
    computeMag(_rawB, _mag);
    pushLine(_mag, _hist[1]);
}

// Flow::Up — frequency across X (rising toward each half's outer edge), time
// down Y with the newest line at the bottom.
void WaterfallMode::renderUp(uint16_t* fb) const {
    for (uint16_t y = 0; y < kH; ++y) {
        // Row 239 is the newest line; each row above it is one frame older.
        const uint16_t age  = (uint16_t)(kH - 1 - y);
        const uint16_t line = (uint16_t)((_head + _lines - age % _lines) % _lines);
        const uint8_t* srcA = _hist[0] + (size_t)line * _cells;
        const uint8_t* srcB = _hist[1] + (size_t)line * _cells;
        uint16_t* dst = fb + (size_t)y * kW;
        for (uint16_t c = 0; c < kHalf; ++c) {
            dst[kHalf - 1 - c] = _lutA[srcA[c]];   // A: rises leftward
            dst[kHalf + c]     = _lutB[srcB[c]];   // B: rises rightward
        }
    }
}

// Flow::Out — frequency up Y (lowest at the bottom), time across X with the
// newest line at the centre and older lines toward both edges.
void WaterfallMode::renderOut(uint16_t* fb) const {
    // One line index per screen column: column 119/120 is the newest, and the
    // age grows outward from there.  Hoisted out of the row loop.
    uint16_t lineFor[kW];
    for (uint16_t x = 0; x < kHalf; ++x) {
        const uint16_t age = (uint16_t)(kHalf - 1 - x);
        const uint16_t ln  = (uint16_t)((_head + _lines - age % _lines) % _lines);
        lineFor[x]             = ln;   // left half (A), newest at the centre
        lineFor[kW - 1 - x]    = ln;   // right half (B), mirrored
    }
    for (uint16_t y = 0; y < kH; ++y) {
        const uint16_t cell = (uint16_t)(kH - 1 - y);   // row 239 = lowest frequency
        uint16_t* dst = fb + (size_t)y * kW;
        for (uint16_t x = 0; x < kHalf; ++x)
            dst[x] = _lutA[_hist[0][(size_t)lineFor[x] * _cells + cell]];
        for (uint16_t x = kHalf; x < kW; ++x)
            dst[x] = _lutB[_hist[1][(size_t)lineFor[x] * _cells + cell]];
    }
}

void WaterfallMode::render(Renderer& r, const ScopeState& /*state*/,
                           const SampleBuffers& /*buf*/) {
    uint16_t* fb = r.tft.getFrameBuffer();
    if (fb == nullptr) return;

    if (_flow == Flow::Up) renderUp(fb);
    else                   renderOut(fb);

    // Centre divider between the two channels.
    r.vline(Theme::CX - 1, Theme::PlotY, Theme::PlotH, Theme::Grid);
    r.vline(Theme::CX,     Theme::PlotY, Theme::PlotH, Theme::Grid);
    // HUD drawn by RunScreen on top afterward.
}
