// modes/TunerMode.cpp — Dual-channel YIN tuner implementation.

#include "TunerMode.h"
#include "../Theme.h"
#include "../Acquisition.h"
#include "../Settings.h"
#include "../Fonts.h"
#include <AcqCore.h>
#include <Arduino.h>   // snprintf
#include <math.h>

// Vertical layout: A fills the top half, B the bottom half, split at the centre
// line.  Each channel shows a large reading centred in its half; in note mode a
// cents bar sits just inside the centre line.
static constexpr int16_t kBigYA = 48;    // ch A reading, top of text
static constexpr int16_t kBigYB = 168;   // ch B reading
static constexpr int16_t kBarYA = 105;   // ch A cents bar (above centre)
static constexpr int16_t kBarYB = 135;   // ch B cents bar (below centre)
static constexpr int16_t kBarHalf = 70;  // cents bar half-width (px), ±50 cents
static constexpr int16_t kInTuneCents = 5;   // |cents| within this reads as in tune

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

void TunerMode::encoderTurn(int8_t delta) {
    if (delta) _showNote = !_showNote;
}

void TunerMode::drawChannel(Renderer& r, float freq, uint16_t color,
                            int16_t bigY, int16_t barY) const {
    char buf[16];

    if (freq <= 0.0f) {
        r.textCenterX(bigY, "--", Theme::Dim, Arial_24);
        return;
    }

    if (!_showNote) {
        snprintf(buf, sizeof buf, "%.1f Hz", (double)freq);
        r.textCenterX(bigY, buf, color, Arial_24);
        return;
    }

    // Note mode: nearest note + cents relative to the A4 reference.
    const float a4 = _settings ? (float)_settings->a4_hz : 440.0f;
    const float midi = 69.0f + 12.0f * log2f(freq / a4);
    const int   nn   = (int)lroundf(midi);
    const int   cents = (int)lroundf((midi - nn) * 100.0f);
    const int   idx  = ((nn % 12) + 12) % 12;
    const int   oct  = nn / 12 - 1;

    snprintf(buf, sizeof buf, "%s%d", kNoteNames[idx], oct);
    r.textCenterX(bigY, buf, color, Arial_24);

    // Cents bar: baseline with a centre (in-tune) tick and a marker whose
    // offset shows the deviation; green when in tune, yellow otherwise.
    r.hline(Theme::CX - kBarHalf, barY, 2 * kBarHalf, Theme::Grid);
    r.vline(Theme::CX, barY - 5, 11, Theme::Text);
    int c = cents;
    if (c < -50) c = -50;
    if (c >  50) c =  50;
    const int16_t mx = (int16_t)(Theme::CX + (c * kBarHalf) / 50);
    const uint16_t mcol = (abs(cents) <= kInTuneCents) ? Theme::TraceA : Theme::Highlight;
    r.fillRect(mx - 2, barY - 6, 5, 13, mcol);
}

void TunerMode::render(Renderer& r, const ScopeState& /*state*/,
                       const SampleBuffers& /*buf*/) {
    // Centre divider between the two channels.
    r.hline(Theme::PlotX, Theme::CY, Theme::PlotW, Theme::Grid);

    drawChannel(r, _freqA, Theme::TraceA, kBigYA, kBarYA);   // top: channel A
    drawChannel(r, _freqB, Theme::TraceB, kBigYB, kBarYB);   // bottom: channel B
    // HUD drawn by RunScreen on top afterward.
}
