// Parameter.cpp — Static descriptor table and stepping logic.
//
// Step tables are fixed arrays of discrete values.  indexOf() finds the
// current position; clampi() constrains the next index so no out-of-range
// access or wrap-around occurs.

#include "Parameter.h"
#include <Arduino.h>   // snprintf on Teensy

// ---- Local helpers --------------------------------------------------------

// Clamp integer v to the closed interval [lo, hi].
static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Return the index of needle in arr (length n), or 0 if not found.
// "Not found" falls back to index 0 so we never return a stale out-of-range
// position (can happen if a value was set by resetToDefaults() outside the
// step table — unlikely but defensive).
template<uint8_t N>
static int indexOf(const uint16_t (&arr)[N], uint16_t needle) {
    for (uint8_t i = 0; i < N; ++i) {
        if (arr[i] == needle) return i;
    }
    return 0;
}

// ---- Timebase (50 µs/div … 10 ms/div, 15 steps) --------------------------
// A 1-1.5-2-3-5-7 sequence per decade for fine control in the ms range.

static void adjTime(ScopeState& s, int8_t d) {
    static const uint16_t steps[] = {50, 70, 100, 150, 200, 300, 500, 700,
                                     1000, 1500, 2000, 3000, 5000, 7000, 10000};
    const int count = sizeof(steps) / sizeof(steps[0]);
    int i = indexOf(steps, s.timebase_us_per_div);
    i = clampi(i + d, 0, count - 1);
    s.timebase_us_per_div = steps[i];
}

// Render the timebase with one decimal of precision for non-integer ms values
// (e.g. 1500 µs → "1.5 ms/div").  Sub-millisecond values stay in µs.
static void fmtTime(const ScopeState& s, char* b, uint8_t n) {
    uint16_t us = s.timebase_us_per_div;
    if (us >= 1000) {
        uint16_t whole = us / 1000;
        uint16_t tenths = (us % 1000) / 100;   // step table never goes finer than 0.1 ms
        if (tenths == 0)
            snprintf(b, n, "%u ms/div", whole);
        else
            snprintf(b, n, "%u.%u ms/div", whole, tenths);
    } else {
        snprintf(b, n, "%u us/div", us);
    }
}

// ---- Voltage scale (100 mV/div … 5 V/div, 6 steps) -----------------------
// Adjusts the channel(s) indicated by s.channel:
//   ChannelSel::A    → ch 0 only
//   ChannelSel::B    → ch 1 only
//   ChannelSel::Both → ch 0 and ch 1

static void adjVScale(ScopeState& s, int8_t d) {
    static const uint16_t steps[] = {100, 200, 500, 1000, 2000, 5000};
    // lo/hi define the inclusive channel index range to update.
    uint8_t lo = (s.channel == ChannelSel::B) ? 1 : 0;
    uint8_t hi = (s.channel == ChannelSel::A) ? 0 : 1;
    for (uint8_t c = lo; c <= hi && c < 2; ++c) {
        // Skip channels whose trace is disabled — V/div on a hidden channel
        // would have no visible effect, so leave its stored scale untouched.
        if (!s.channelEnabled[c]) continue;
        int i = indexOf(steps, s.vscale_mv_per_div[c]);
        i = clampi(i + d, 0, 5);
        s.vscale_mv_per_div[c] = steps[i];
    }
}

// Display the value for the currently selected channel: ch0 for A or Both,
// ch1 for B.  In Both mode ch0 is shown (both channels are edited together,
// so either would be accurate; ch0 is the conventional lead channel).
static void fmtVScale(const ScopeState& s, char* b, uint8_t n) {
    uint8_t c = (s.channel == ChannelSel::B) ? 1 : 0;
    snprintf(b, n, "%u mV/div", s.vscale_mv_per_div[c]);
}

// ---- Trigger level (–10 000 mV … +10 000 mV, 100 mV/step) ---------------

static void adjTrig(ScopeState& s, int8_t d) {
    s.trigger_level_mv = (int16_t)clampi(
        (int)s.trigger_level_mv + (int)d * 100,
        -10000, 10000);
}

static void fmtTrig(const ScopeState& s, char* b, uint8_t n) {
    snprintf(b, n, "%d mV", s.trigger_level_mv);
}

// ---- Static descriptor table (order matches EncoderParam enum) ------------
// The table is indexed directly by (uint8_t)EncoderParam, so the order MUST
// match the enum declaration in ScopeState.h:
//   Timebase = 0, VScale = 1, TriggerLevel = 2.

static_assert((uint8_t)EncoderParam::Timebase == 0, "kParams order must match EncoderParam");
static_assert((uint8_t)EncoderParam::VScale == 1, "kParams order must match EncoderParam");
static_assert((uint8_t)EncoderParam::TriggerLevel == 2, "kParams order must match EncoderParam");

static const Parameter kParams[] = {
    { EncoderParam::Timebase,     "Time",   adjTime,   fmtTime   },
    { EncoderParam::VScale,       "V/div",  adjVScale, fmtVScale },
    { EncoderParam::TriggerLevel, "Trig",   adjTrig,   fmtTrig   },
};

// ---- Public API -----------------------------------------------------------

const Parameter& parameterFor(EncoderParam id) {
    return kParams[(uint8_t)id];
}

bool paramAppliesInMode(EncoderParam id, Mode m) {
    switch (m) {
        case Mode::Triggered:
            // All three parameters apply in Triggered mode.
            return true;
        case Mode::Rolling:
            // TriggerLevel has no meaning when free-running.
            return id != EncoderParam::TriggerLevel;
        case Mode::XY:
            // Only voltage scale makes sense in XY mode.
            return id == EncoderParam::VScale;
        default:
            return false;
    }
}

EncoderParam nextSelectable(const ScopeState& s) {
    // There are exactly three parameters.  VScale always applies in every mode,
    // so this loop is guaranteed to terminate within at most 3 iterations.
    const int kCount = 3;
    int i = ((int)s.selected + 1) % kCount;
    for (int guard = 0; guard < kCount; ++guard, i = (i + 1) % kCount) {
        if (paramAppliesInMode((EncoderParam)i, s.mode))
            return (EncoderParam)i;
    }
    // Fallback: should never be reached (VScale is always valid).
    return EncoderParam::VScale;
}

void clampSelectable(ScopeState& s) {
    if (!paramAppliesInMode(s.selected, s.mode)) {
        s.selected = nextSelectable(s);
    }
}
