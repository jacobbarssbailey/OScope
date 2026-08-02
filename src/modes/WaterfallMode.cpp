// modes/WaterfallMode.cpp — Scrolling spectrogram implementation.

#include "WaterfallMode.h"
#include "../Theme.h"
#include "../Acquisition.h"
#include <string.h>   // memmove
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
        _lutA[v] = rampColor(Theme::TraceA, v);   // channel A: background -> green
        _lutB[v] = rampColor(Theme::TraceB, v);   // channel B: background -> cyan
    }

    // Fixed sample rate (shared with Spectrum): interval = timebase*GridCols/N
    // (integer µs), Fs = 1e6/interval.
    uint32_t interval = (kWaterfallTimebaseUs * (uint32_t)Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    const float fs    = 1000000.0f / (float)interval;
    const float binHz = fs / (float)kFFT;

    // Map each screen column to the nearest FFT bin across [kFminHz, kFmaxHz].
    for (uint16_t x = 0; x < kW; ++x) {
        const float freq = kFminHz + (x + 0.5f) * (kFmaxHz - kFminHz) / (float)kW;
        int bin = (int)(freq / binHz + 0.5f);
        if (bin < 1) bin = 1;
        if (bin > kNBin) bin = kNBin;
        _col2bin[x] = (uint8_t)(bin - 1);
    }

    memset(_histA, 0, sizeof _histA);
    memset(_histB, 0, sizeof _histB);
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

void WaterfallMode::pushRow(const float* mag, uint8_t (*hist)[kW], bool towardTop) {
    // Precompute the intensity of each bin once, then fan out to the columns.
    uint8_t intBin[kNBin];
    for (uint16_t k = 0; k < kNBin; ++k) intBin[k] = magToIntensity(mag[k]);

    if (towardTop) {
        // Channel A: shift rows toward row 0 (the top edge); newest at kRows-1.
        memmove(&hist[0][0], &hist[1][0], (size_t)(kRows - 1) * kW);
        uint8_t* row = hist[kRows - 1];
        for (uint16_t x = 0; x < kW; ++x) row[x] = intBin[_col2bin[x]];
    } else {
        // Channel B: shift rows toward the bottom edge; newest at row 0.
        memmove(&hist[1][0], &hist[0][0], (size_t)(kRows - 1) * kW);
        uint8_t* row = hist[0];
        for (uint16_t x = 0; x < kW; ++x) row[x] = intBin[_col2bin[x]];
    }
}

void WaterfallMode::onFrame(const SampleBuffers& /*buf*/) {
    if (!_src || !_src->readNewestBlock(_rawA, _rawB, kFFT)) return;  // hold last
    computeMag(_rawA, _mag);
    pushRow(_mag, _histA, true);    // A grows toward the top
    computeMag(_rawB, _mag);
    pushRow(_mag, _histB, false);   // B grows toward the bottom
}

void WaterfallMode::render(Renderer& r, const ScopeState& /*state*/,
                           const SampleBuffers& /*buf*/) {
    uint16_t* fb = r.tft.getFrameBuffer();
    if (fb == nullptr) return;

    // Channel A: history rows map straight to screen rows 0..kRows-1.
    for (uint16_t i = 0; i < kRows; ++i) {
        uint16_t* dst = fb + (int)i * kW;
        const uint8_t* src = _histA[i];
        for (uint16_t x = 0; x < kW; ++x) dst[x] = _lutA[src[x]];
    }
    // Channel B: rows map to screen rows kRows+2 .. (below the 2px centre band).
    const uint16_t bTop = kRows + 2;   // 121
    for (uint16_t i = 0; i < kRows; ++i) {
        uint16_t* dst = fb + (int)(bTop + i) * kW;
        const uint8_t* src = _histB[i];
        for (uint16_t x = 0; x < kW; ++x) dst[x] = _lutB[src[x]];
    }
    // Centre divider (the 2px band between the two channels).
    r.hline(Theme::PlotX, kRows, Theme::PlotW, Theme::Grid);
    // HUD drawn by RunScreen on top afterward.
}
