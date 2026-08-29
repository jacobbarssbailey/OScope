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

static int indexOf(const uint32_t* arr, uint8_t n, uint32_t needle) {
    for (uint8_t i = 0; i < n; ++i) {
        if (arr[i] == needle) return i;
    }
    return 0;
}

// ---- Timebase (per-mode range) -------------------------------------------
// A 1-1.5-2-3-5-7 sequence per decade for fine control.  The three modes cover
// different ranges — Triggered is bounded by trigger-search cost at the fast end
// and sweep-fill time at the slow end; the free-running modes can go much slower
// now that their frame rate no longer falls with the sample rate (see
// Acquisition::updateFreeRunning).  All three share the same sequence, so the
// tables are prefixes/suffixes of one another and a value carried across a mode
// switch usually lands on an exact step.
//   Triggered : 50 µs … 10 ms
//   Rolling   : 500 µs … 1 s
//   XY        : 500 µs … 100 ms

static const uint32_t kTimeTrig[] = {
    50, 70, 100, 150, 200, 300, 500, 700,
    1000, 1500, 2000, 3000, 5000, 7000, 10000};
static const uint32_t kTimeRoll[] = {
    500, 700, 1000, 1500, 2000, 3000, 5000, 7000,
    10000, 15000, 20000, 30000, 50000, 70000,
    100000, 150000, 200000, 300000, 500000, 700000, 1000000};
static const uint32_t kTimeXY[] = {
    500, 700, 1000, 1500, 2000, 3000, 5000, 7000,
    10000, 15000, 20000, 30000, 50000, 70000, 100000};

// Step table (values + length) for the given mode's timebase.
static void timeStepsFor(Mode m, const uint32_t** v, uint8_t* n) {
    switch (m) {
        case Mode::Rolling: *v = kTimeRoll; *n = sizeof(kTimeRoll)/sizeof(kTimeRoll[0]); break;
        case Mode::XY:      *v = kTimeXY;   *n = sizeof(kTimeXY)  /sizeof(kTimeXY[0]);   break;
        default:            *v = kTimeTrig; *n = sizeof(kTimeTrig)/sizeof(kTimeTrig[0]); break;
    }
}

static void adjTime(ScopeState& s, int8_t d) {
    const uint32_t* steps; uint8_t count;
    timeStepsFor(s.mode, &steps, &count);
    int i = indexOf(steps, count, s.timebase());
    i = clampi(i + d, 0, count - 1);
    s.setTimebase(steps[i]);
}

// Render the timebase with one decimal for non-integer values (e.g. 1500 µs →
// "1.5" + "ms").  µs below 1 ms, ms below 1 s, seconds at/above 1 s.
//
// The unit is bare rather than "ms/div": the settings overlay always shows it
// beside the timebase icon, which already says what the number measures, and
// "/div" costs more width than the round face has next to a 32 px icon.
static void fmtTime(const ScopeState& s, char* b, uint8_t n,
                    char* unit, uint8_t nu) {
    const uint32_t us = s.timebase();
    uint32_t whole, tenths;
    if (us >= 1000000) {
        whole  = us / 1000000;
        tenths = (us % 1000000) / 100000;
        snprintf(unit, nu, "s");
    } else if (us >= 1000) {
        whole  = us / 1000;
        tenths = (us % 1000) / 100;   // step table never goes finer than 0.1 ms
        snprintf(unit, nu, "ms");
    } else {
        whole  = us;
        tenths = 0;
        snprintf(unit, nu, "us");
    }
    if (tenths == 0) snprintf(b, n, "%lu", (unsigned long)whole);
    else             snprintf(b, n, "%lu.%lu", (unsigned long)whole,
                              (unsigned long)tenths);
}

// ---- Voltage scale (50 mV/div … 5 V/div, 12 steps) -----------------------
// A 1-1.5-2-3-5-7 sequence for fine control (roughly doubles the old 6 steps).
// Adjusts the channel(s) indicated by s.channel:
//   ChannelSel::A    → ch 0 only
//   ChannelSel::B    → ch 1 only
//   ChannelSel::Both → ch 0 and ch 1

static void adjVScale(ScopeState& s, int8_t d) {
    static const uint16_t steps[] = {50, 100, 150, 200, 300, 500,
                                     700, 1000, 1500, 2000, 3000, 5000};
    const int count = sizeof(steps) / sizeof(steps[0]);
    // lo/hi define the inclusive channel index range to update.
    uint8_t lo = (s.channel == ChannelSel::B) ? 1 : 0;
    uint8_t hi = (s.channel == ChannelSel::A) ? 0 : 1;
    for (uint8_t c = lo; c <= hi && c < 2; ++c) {
        // Skip channels whose trace is disabled — V/div on a hidden channel
        // would have no visible effect, so leave its stored scale untouched.
        if (!s.channelEnabled[c]) continue;
        int i = indexOf(steps, s.vscale_mv_per_div[c]);
        i = clampi(i + d, 0, count - 1);
        s.vscale_mv_per_div[c] = steps[i];
    }
}

// Display the value for the currently selected channel: ch0 for A or Both,
// ch1 for B.  In Both mode ch0 is shown (both channels are edited together,
// so either would be accurate; ch0 is the conventional lead channel).
// Values ≥ 1 V show in volts (with one decimal for fractional values).
static void fmtVScale(const ScopeState& s, char* b, uint8_t n,
                      char* unit, uint8_t nu) {
    uint8_t c = (s.channel == ChannelSel::B) ? 1 : 0;
    uint16_t mv = s.vscale_mv_per_div[c];
    if (mv >= 1000) {
        uint16_t whole = mv / 1000;
        uint16_t tenths = (mv % 1000) / 100;   // step table never finer than 0.1 V
        snprintf(unit, nu, "V");
        if (tenths == 0) snprintf(b, n, "%u", whole);
        else             snprintf(b, n, "%u.%u", whole, tenths);
    } else {
        snprintf(unit, nu, "mV");
        snprintf(b, n, "%u", mv);
    }
}

// ---- Trigger level (screen-relative) ------------------------------------
// The trigger level moves in proportion to what's on screen rather than in a
// fixed 100 mV step over the full ±10 V input.  A flat step made the level feel
// dead: on a typical signal one 100 mV click shifted the trigger point by well
// under a pixel, and you'd never reach the trace's extremes in a sane number of
// clicks.  Instead:
//   step  = 0.2 division per detent   (vscale / 5)
//   range = ±4 divisions (the visible half-screen), capped at the ±10 V hardware
//           limit — so pushing the level off the top/bottom of the trace stops
//           the trigger, which is the expected behavior.
// Uses the lead channel's V/div (ch 0), matching the V/div readout.

static void adjTrig(ScopeState& s, int8_t d) {
    const int vdiv  = s.vscale_mv_per_div[0];
    const int step  = vdiv / 5;                        // 0.2 div/detent (min via clamp below)
    int       range = 4 * vdiv;                        // ±4 divisions
    if (range > 10000) range = 10000;                  // cap at hardware ±10 V
    s.trigger_level_mv = (int16_t)clampi(
        (int)s.trigger_level_mv + (int)d * (step > 0 ? step : 1),
        -range, range);
}

static void fmtTrig(const ScopeState& s, char* b, uint8_t n,
                    char* unit, uint8_t nu) {
    snprintf(b, n, "%d", s.trigger_level_mv);
    snprintf(unit, nu, "mV");
}

// ---- Static descriptor table (order matches EncoderParam enum) ------------
// The table is indexed directly by (uint8_t)EncoderParam, so the order MUST
// match the enum declaration in ScopeState.h:
//   Timebase = 0, VScale = 1, TriggerLevel = 2.

static_assert((uint8_t)EncoderParam::Timebase == 0, "kParams order must match EncoderParam");
static_assert((uint8_t)EncoderParam::VScale == 1, "kParams order must match EncoderParam");
static_assert((uint8_t)EncoderParam::TriggerLevel == 2, "kParams order must match EncoderParam");

static const Parameter kParams[] = {
    { EncoderParam::Timebase,     "time",    &IconTimebase, adjTime,   fmtTime   },
    { EncoderParam::VScale,       "volts",   &IconVScale,   adjVScale, fmtVScale },
    { EncoderParam::TriggerLevel, "trigger", &IconTrigger,  adjTrig,   fmtTrig   },
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
            // V/div scales both axes; Timebase sets the sample rate, which
            // controls how much of each signal's period the figure spans (and
            // thus how completely a slow Lissajous closes).  Trigger has no
            // meaning when free-running.
            return id != EncoderParam::TriggerLevel;
        case Mode::Spectrum:
            // No adjustable acquisition parameters: the sample rate is fixed
            // (kSpectrumTimebaseUs), the vertical scale is a fixed dBFS mapping,
            // and there is no trigger.  The encoder has nothing to control here.
            return false;
        case Mode::Tuner:
            // Tuner owns the encoder itself (toggles Hz/Note), so none of the
            // shared acquisition parameters apply.
            return false;
        case Mode::Waterfall:
            // Fixed sample rate and colormap, no trigger — nothing to adjust.
            return false;
        default:
            return false;
    }
}

EncoderParam nextSelectable(const ScopeState& s) {
    // There are exactly three parameters.  In the scope modes at least VScale
    // always applies, so the loop finds one within 3 iterations; Spectrum has
    // none, and the fallback (VScale) is returned but never adjusted because
    // handleEvent gates encoder turns on paramAppliesInMode.
    const int kCount = (int)EncoderParam::COUNT;
    int i = ((int)s.selected + 1) % kCount;
    for (int guard = 0; guard < kCount; ++guard, i = (i + 1) % kCount) {
        if (paramAppliesInMode((EncoderParam)i, s.mode))
            return (EncoderParam)i;
    }
    return EncoderParam::VScale;
}

void clampSelectable(ScopeState& s) {
    if (!paramAppliesInMode(s.selected, s.mode)) {
        s.selected = nextSelectable(s);
    }
}
