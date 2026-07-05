// modes/RollingMode.cpp — Free-running scrolling waveform implementation.
//
// Each frame RunScreen fills `buf` with a fresh free-running sweep (no trigger).
// We append it to the history ring and redraw the retained window.
//
// NOTE: acquisition currently produces a full N-sample sweep per frame, so the
// visible window advances up to one sweep per frame.  For signals slower than
// one sweep this reads as a smooth right-to-left scroll; fast signals step by a
// sweep at a time.  Sub-frame scrolling would require incremental capture — a
// later refinement, out of scope for this milestone.

#include "RollingMode.h"
#include "../Mapping.h"
#include "../Theme.h"

void RollingMode::ingest(const SampleBuffers& buf) {
    // Samples arrive oldest→newest; append so the newest lands at the head.
    for (uint16_t i = 0; i < buf.count; ++i) {
        _ring[0][_head] = buf.ch[0][i];
        _ring[1][_head] = buf.ch[1][i];
        _head = (uint16_t)((_head + 1) % N);
        if (_count < N) ++_count;
    }
}

void RollingMode::drawChannel(Renderer& r, uint8_t ch, uint16_t vscale,
                              uint16_t color) const {
    // Oldest retained sample sits _count slots behind the head.
    uint16_t idx = (uint16_t)((_head + N - _count) % N);
    int16_t x0 = Theme::PlotX;
    int16_t y0 = Mapping::sampleToY(_ring[ch][idx], vscale);
    for (uint16_t k = 1; k < _count; ++k) {
        idx = (uint16_t)((idx + 1) % N);
        const int16_t x1 = (int16_t)(Theme::PlotX + k);
        const int16_t y1 = Mapping::sampleToY(_ring[ch][idx], vscale);
        r.line(x0, y0, x1, y1, color);
        x0 = x1;
        y0 = y1;
    }
}

void RollingMode::render(Renderer& r, const ScopeState& state,
                         const SampleBuffers& buf) {
    // Grid is drawn by RunScreen (gated on settings.grid) before this call.
    // Fold this frame's fresh sweep into the persistent history.
    ingest(buf);
    if (_count < 2) return;

    // Draw B first so A lands on top (matches Triggered mode's Z-order).
    if (state.channelEnabled[1]) {
        drawChannel(r, 1, state.vscale_mv_per_div[1], Theme::TraceB);
    }
    if (state.channelEnabled[0]) {
        drawChannel(r, 0, state.vscale_mv_per_div[0], Theme::TraceA);
    }
    // HUD drawn by RunScreen on top afterward.
}
