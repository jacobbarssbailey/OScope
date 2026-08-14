// modes/TunerMode.cpp — Dual-channel YIN tuner implementation.

#include "TunerMode.h"
#include "../Theme.h"
#include "../Acquisition.h"
#include "../Settings.h"
#include "../Fonts.h"
#include <AcqCore.h>
#include <Arduino.h>   // snprintf
#include <math.h>

// Vertical layout: channel A fills the top half, B the bottom, with a cents
// meter for each flanking the centre.  A's big readout sits high, B's low; the
// two meters sit just above/below centre (no divider line — the meters read as
// the split).  Readout = a large glyph (note or frequency) with a small
// subscript (octave or "Hz").
static constexpr int16_t kBigYA   = 34;   // ch A readout, top of text
static constexpr int16_t kMeterYA = 96;   // ch A cents meter (above centre)
static constexpr int16_t kMeterYB = 144;  // ch B cents meter (below centre)
static constexpr int16_t kBigYB   = 164;  // ch B readout, top of text

static constexpr int16_t kMeterHalf   = 90;  // meter half-width (px) = ±50 cents
static constexpr int16_t kSubDrop     = 15;  // subscript baseline drop below the glyph
static constexpr int16_t kSubGap      = 2;   // gap between glyph and subscript
static constexpr int16_t kInTuneCents = 5;   // |cents| within this reads as in tune
static constexpr int     kMeterTicks  = 17;  // ruler ticks; centre + steps of 12.5%

// Near-white ruler ticks (everything but the pure-white centre major tick).
static constexpr uint16_t kTickColor = 0xDEFB;   // ~#DBDBDB

static const char* kNoteNames[12] =
    {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Blend an RGB565 colour halfway to white — used to make the in-tune marker glow.
static uint16_t brighten(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (uint16_t)(r + (31 - r) / 2);
    g = (uint16_t)(g + (63 - g) / 2);
    b = (uint16_t)(b + (31 - b) / 2);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

TunerMode::TunerMode() {
    // Fixed sample rate, derived like Acquisition: interval = timebase*GridCols/N
    // (integer µs), Fs = 1e6/interval.
    uint32_t interval = (kTunerTimebaseUs * (uint32_t)Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    _fsHz = 1000000.0f / (float)interval;
}

float TunerMode::analyze(const uint16_t* raw) {
    uint16_t mn = 0xFFFF, mx = 0;
    for (uint16_t i = 0; i < kWin; ++i) {
        const uint16_t v = raw[i];
        _x[i] = (float)v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if ((uint16_t)(mx - mn) < kMinPP) return -1.0f;   // too quiet: no pitch

    const float period = AcqCore::yinPeriod(_x, kWin, _diff, kYinThresh);
    if (period <= 0.0f) return -1.0f;
    return _fsHz / period;
}

float TunerMode::smooth(float prev, float fresh) {
    if (fresh <= 0.0f) return -1.0f;                       // lost the pitch
    if (prev <= 0.0f) return fresh;                         // reacquired
    if (fabsf(fresh - prev) > 0.05f * prev) return fresh;   // big jump: snap
    return prev * 0.7f + fresh * 0.3f;                      // small jitter: smooth
}

void TunerMode::onFrame(const SampleBuffers& /*buf*/) {
    if (!_src || !_src->readNewestBlock(_rawA, _rawB, kWin)) return;  // hold last
    _freqA = smooth(_freqA, analyze(_rawA));
    _freqB = smooth(_freqB, analyze(_rawB));
}

void TunerMode::encoderTurn(int8_t delta) {
    if (delta) _showNote = !_showNote;
}

// Draw "<main><sub>" centred as a unit: a large glyph with a small subscript
// dropped toward the baseline (e.g. note + octave, or frequency + "Hz").
static void drawReadout(Renderer& r, const char* main, const char* sub,
                        uint16_t color, int16_t topY) {
    const int16_t mw = r.textWidth(main, FONT_LARGE);
    const int16_t sw = r.textWidth(sub, FONT_BODY);
    const int16_t x  = (int16_t)(Theme::CX - (mw + kSubGap + sw) / 2);
    r.text(x, topY, main, color, FONT_LARGE);
    r.text((int16_t)(x + mw + kSubGap), (int16_t)(topY + kSubDrop), sub, color, FONT_BODY);
}

// Draw the cents meter: a tick ruler centred at cy (taller, brighter centre
// tick), with a marker at the deviation when `haveMarker`; the marker brightens
// when the reading is within tolerance.
static void drawMeter(Renderer& r, int16_t cy, int cents, bool haveMarker,
                      uint16_t color) {
    const int16_t left   = Theme::CX - kMeterHalf;
    const int     centre = kMeterTicks / 2;   // 8 -> 0 cents
    for (int i = 0; i < kMeterTicks; ++i) {
        const int16_t x = (int16_t)(left + (2 * kMeterHalf * i) / (kMeterTicks - 1));
        const int     d = abs(i - centre);    // steps of 12.5% from centre
        int16_t  h;
        uint16_t col;
        if (d == 0)                 { h = 15; col = Theme::Text; }  // major (centre)
        else if (d == 2 || d == 4)  { h = 11; col = kTickColor; }   // medium (±25%, ±50%)
        else                        { h = 7;  col = kTickColor; }   // minor
        r.vline(x, (int16_t)(cy - h / 2), h, col);
    }
    if (haveMarker) {
        int c = cents;
        if (c < -50) c = -50;
        if (c >  50) c =  50;
        const int16_t  mx  = (int16_t)(Theme::CX + (c * kMeterHalf) / 50);
        const uint16_t col = (abs(cents) <= kInTuneCents) ? brighten(color) : color;
        r.fillRect((int16_t)(mx - 1), (int16_t)(cy - 11), 3, 23, col);
    }
}

void TunerMode::drawChannel(Renderer& r, float freq, uint16_t color,
                            int16_t bigY, int16_t meterY) const {
    if (freq <= 0.0f) {
        r.textCenterX(bigY, "--", Theme::Dim, FONT_LARGE);
        drawMeter(r, meterY, 0, false, color);   // ruler only, no marker
        return;
    }

    // Nearest note + cents relative to the A4 reference — computed in both modes
    // so the meter is always a live tuning aid; only the big readout toggles.
    const float a4    = _settings ? (float)_settings->a4_hz : 440.0f;
    const float midi  = 69.0f + 12.0f * log2f(freq / a4);
    const int   nn    = (int)lroundf(midi);
    const int   cents = (int)lroundf((midi - nn) * 100.0f);
    const int   idx   = ((nn % 12) + 12) % 12;
    const int   oct   = nn / 12 - 1;

    char sub[8];
    if (_showNote) {
        snprintf(sub, sizeof sub, "%d", oct);
        drawReadout(r, kNoteNames[idx], sub, color, bigY);
    } else {
        char hz[8];
        snprintf(hz, sizeof hz, "%.0f", (double)freq);
        drawReadout(r, hz, "Hz", color, bigY);
    }

    drawMeter(r, meterY, cents, true, color);
}

void TunerMode::render(Renderer& r, const ScopeState& /*state*/,
                       const SampleBuffers& /*buf*/) {
    drawChannel(r, _freqA, Theme::TraceA, kBigYA, kMeterYA);   // top: channel A
    drawChannel(r, _freqB, Theme::TraceB, kBigYB, kMeterYB);   // bottom: channel B
    // HUD drawn by RunScreen on top afterward.
}
