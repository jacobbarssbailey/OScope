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
static constexpr int16_t kMeterYA = 96;   // ch A cents meter (above centre)
static constexpr int16_t kMeterYB = 144;  // ch B cents meter (below centre)

static constexpr int16_t kMeterHalf   = 90;  // meter half-width (px) = ±50 cents
static constexpr int16_t kInTuneCents = 5;   // |cents| within this reads as in tune
static constexpr int     kMeterTicks  = 17;  // ruler ticks; centre + steps of 12.5%

// Deviation marker: a 3 × 30 px rounded bar.  When the reading is in tune it
// gains an outline offset kMarkOutset px around it (the marker used to simply
// brighten, which was lost against the ruler).
static constexpr int16_t kMarkW      = 3;
static constexpr int16_t kMarkH      = 30;
static constexpr int16_t kMarkR      = 1;   // bar corner radius
static constexpr int16_t kMarkOutset = 3;   // gap between bar and in-tune outline
static constexpr int16_t kMarkOutR   = 4;   // outline corner radius

// The readouts sit in the gap between the screen edge and their channel's
// meter, with equal margin above and below: for A that gap is 0 → marker top,
// for B it is marker bottom → 240.  Both readouts are one FONT_LARGE cap tall.
static constexpr int16_t kBigCapH  = 37;                    // FONT_LARGE cap height
static constexpr int16_t kMarkTopA = kMeterYA - kMarkH / 2; // 81
static constexpr int16_t kMarkBotB = kMeterYB + kMarkH / 2; // 159
static constexpr int16_t kBigYA    = (kMarkTopA - kBigCapH) / 2;                  // 22
static constexpr int16_t kBigYB    = kMarkBotB + (Theme::H - kMarkBotB - kBigCapH) / 2;  // 181

// Ruler ticks are a dark grey so the coloured marker stays legible in front of
// them (they used to be near-white and swamped it).
static constexpr uint16_t kTickColor = Theme::DimDark;   // #404040

static const char* kNoteNames[12] =
    {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

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

void TunerMode::encoderPress() {
    _showNote = !_showNote;
}

// Draw the cents meter: a tick ruler centred at cy (a taller centre tick), with
// a marker at the deviation when `haveMarker`; the marker gains an offset
// outline when the reading is within tolerance.
static void drawMeter(Renderer& r, int16_t cy, int cents, bool haveMarker,
                      uint16_t color) {
    const int16_t left   = Theme::CX - kMeterHalf;
    const int     centre = kMeterTicks / 2;   // 8 -> 0 cents
    for (int i = 0; i < kMeterTicks; ++i) {
        const int16_t x = (int16_t)(left + (2 * kMeterHalf * i) / (kMeterTicks - 1));
        const int     d = abs(i - centre);    // steps of 12.5% from centre
        int16_t h;
        if (d == 0)                 h = 15;   // major (centre)
        else if (d == 2 || d == 4)  h = 11;   // medium (±25%, ±50%)
        else                        h = 7;    // minor
        r.vline(x, (int16_t)(cy - h / 2), h, kTickColor);
    }
    if (haveMarker) {
        int c = cents;
        if (c < -50) c = -50;
        if (c >  50) c =  50;
        const int16_t mx = (int16_t)(Theme::CX + (c * kMeterHalf) / 50);
        const int16_t x  = (int16_t)(mx - kMarkW / 2);
        const int16_t y  = (int16_t)(cy - kMarkH / 2);
        r.fillRoundRect(x, y, kMarkW, kMarkH, kMarkR, color);
        if (abs(cents) <= kInTuneCents) {
            r.drawRoundRect((int16_t)(x - kMarkOutset), (int16_t)(y - kMarkOutset),
                            (int16_t)(kMarkW + 2 * kMarkOutset),
                            (int16_t)(kMarkH + 2 * kMarkOutset), kMarkOutR, color);
        }
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

    // The readout is white; the channel colour lives in the meter marker.
    if (_showNote) {
        char oc[8];
        snprintf(oc, sizeof oc, "%d", oct);
        r.textUnitCenterX(bigY, kNoteNames[idx], oc, Theme::Text,
                          FONT_LARGE, FONT_BODY);
    } else {
        char hz[8];
        snprintf(hz, sizeof hz, "%.0f", (double)freq);
        r.textUnitCenterX(bigY, hz, "Hz", Theme::Text, FONT_LARGE, FONT_BODY);
    }

    drawMeter(r, meterY, cents, true, color);
}

void TunerMode::render(Renderer& r, const ScopeState& /*state*/,
                       const SampleBuffers& /*buf*/) {
    drawChannel(r, _freqA, Theme::TraceA, kBigYA, kMeterYA);   // top: channel A
    drawChannel(r, _freqB, Theme::TraceB, kBigYB, kMeterYB);   // bottom: channel B
    // HUD drawn by RunScreen on top afterward.
}
