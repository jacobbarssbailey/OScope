// Acquisition.cpp — ADC sampling engine implementation.
//
// Voltage ↔ ADC mapping (repeat of ScopeMode.h header comment for locality):
//   Hardware: ±10 V input → 0..3.3 V at ADC pin (level-shift + attenuator).
//   ADC: 10-bit, so full range = 0..1023 counts.
//   Mid-rail (0 V input) = 512 counts (nominal; hardware may vary ±10 counts).
//   Scale: 10000 mV / 512 counts ≈ 19.53 mV per count.
//   Trigger threshold (mV → ADC count):
//     trig_adc = ADC_MID + trigger_level_mv * ADC_MID / 10000
//   Example: trigger_level_mv = 1000 mV (1 V) → trig_adc = 512 + 51 = 563
//
// Timebase → sample interval:
//   GridCols         = 8 divisions across the 240-pixel display
//   Samples per div  = N / GridCols = 240 / 8 = 30
//   sample_interval  = timebase_us_per_div / (N / GridCols)
//                    = timebase_us_per_div * GridCols / N   [µs]
//   Minimum interval = 1 µs (analogRead takes ~1–2 µs on Teensy 4.0).
//
// Trigger search:
//   Source: channel A (fixed; configurable trigger source arrives in Milestone 7).
//   Edge:   rising (fixed; configurable in Milestone 7).
//   Window: TRIGGER_SEARCH_SAMPLES pre-samples searched before giving up and
//           free-running.  This bounds the blocking time to
//           TRIGGER_SEARCH_SAMPLES * sample_interval µs (worst case).
//   TRIGGER_SEARCH_SAMPLES is set to 2*N = 480 — two full sweep widths — so
//   the trigger always catches a crossing if the signal period ≤ 2 sweeps.
//   NOTE: fixed trigger source/edge — see Task 7 for configurable Source/Edge/Mode.

#include "Acquisition.h"
#include "Config.h"
#include "Theme.h"

#include <Arduino.h>

// Nominal ADC count for 0 V input (mid-rail).  Hardware-calibrate if needed.
static constexpr int32_t ADC_MID = 512;

// Number of pre-samples to search for the trigger crossing before free-running.
// Two full sweep widths provides reliable trigger lock for signals of period
// up to ~2× the current sweep time.
static constexpr uint16_t TRIGGER_SEARCH_SAMPLES = 2 * SampleBuffers::N;

// Convert trigger_level_mv to an ADC count threshold.
// Clamped to [0, 1023] so the trigger threshold is always reachable.
static uint16_t triggerADC(int16_t trigger_level_mv) {
    // trig_adc = ADC_MID + trigger_level_mv * ADC_MID / 10000
    // Use int32 to avoid overflow: max |trigger_level_mv| = 10000 → 512*10000/10000 = 512.
    int32_t adc = ADC_MID + ((int32_t)trigger_level_mv * ADC_MID) / 10000;
    if (adc < 0)    adc = 0;
    if (adc > 1023) adc = 1023;
    return (uint16_t)adc;
}

// Compute the inter-sample delay in µs from the current timebase setting.
// See header comment for the formula.
static uint32_t sampleIntervalUs(uint16_t timebase_us_per_div) {
    // interval = timebase_us_per_div * GridCols / N
    // GridCols = 8, N = 240 → multiply first to preserve precision.
    uint32_t interval = ((uint32_t)timebase_us_per_div * Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    return interval;
}

// --------------------------------------------------------------------------

void Acquisition::begin() {
    analogReadResolution(10);   // 0..1023 raw counts
}

bool Acquisition::capture(const ScopeState& state, SampleBuffers& buf) {
    const uint32_t interval = sampleIntervalUs(state.timebase_us_per_div);
    const uint16_t trig_adc = triggerADC(state.trigger_level_mv);

    // Non-triggered modes free-run; every completed sweep counts as a
    // successful single-shot capture.  Triggered mode overrides this below.
    bool triggered = true;

    // --- Trigger search (Triggered mode only; channel A rising edge) ---
    // NOTE: trigger source is fixed to channel A; SIGNAL_A is read
    // unconditionally here regardless of state.channelEnabled[0].
    // Configurable trigger source/edge arrives in Milestone 7.
    if (state.mode == Mode::Triggered) {
        uint16_t prev = (uint16_t)analogRead(SIGNAL_A);
        bool found = false;

        for (uint16_t i = 0; i < TRIGGER_SEARCH_SAMPLES; ++i) {
            delayMicroseconds(interval);
            uint16_t cur = (uint16_t)analogRead(SIGNAL_A);
            // Rising edge: previous sample below threshold, current at/above.
            if (prev < trig_adc && cur >= trig_adc) {
                found = true;
                break;
            }
            prev = cur;
        }
        // If no crossing found within the search window: free-run (fall through)
        // so the display never hangs when the signal is absent or the trigger
        // level is unreachable.  Report the free-run so single-shot keeps
        // waiting for a genuine trigger rather than freezing on noise.
        triggered = found;
    }

    // --- Sweep: read N samples for each enabled channel ---
    for (uint16_t i = 0; i < SampleBuffers::N; ++i) {
        if (state.channelEnabled[0]) {
            buf.ch[0][i] = (uint16_t)analogRead(SIGNAL_A);
        }
        if (state.channelEnabled[1]) {
            buf.ch[1][i] = (uint16_t)analogRead(SIGNAL_B);
        }
        if (i < SampleBuffers::N - 1) {
            delayMicroseconds(interval);
        }
    }
    buf.count = SampleBuffers::N;
    return triggered;
}
