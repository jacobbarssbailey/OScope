// Acquisition.cpp — Non-blocking ADC acquisition state machine implementation.
//
// Voltage ↔ ADC mapping (nominal, hardware-calibrate if needed):
//   ±10 V input → 0..3.3 V at the ADC pin → 0..1023 counts (10-bit).
//   Mid-rail (0 V input) = 512 counts.  ~19.53 mV/count.
//   Trigger threshold: trig_adc = 512 + trigger_level_mv * 512 / 10000.
//
// Timebase → sample interval:
//   interval_us = timebase_us_per_div * GridCols / N  (GridCols=8, N=240).
//   Minimum 1 µs.
//
// See Acquisition.h for the state-machine and pacing description.

#include "Acquisition.h"
#include "Config.h"
#include "Theme.h"

#include <Arduino.h>

// Nominal ADC count for 0 V input (mid-rail).
static constexpr int32_t ADC_MID = 512;

// Trigger-search window: two full sweep widths, so a periodic signal of period
// up to ~2 sweeps always presents a crossing before the window is exhausted.
static constexpr uint16_t TRIGGER_SEARCH_SAMPLES = 2 * SampleBuffers::N;

// Convert trigger_level_mv to an ADC count threshold, clamped to [0, 1023].
static uint16_t triggerADC(int16_t trigger_level_mv) {
    int32_t adc = ADC_MID + ((int32_t)trigger_level_mv * ADC_MID) / 10000;
    if (adc < 0)    adc = 0;
    if (adc > 1023) adc = 1023;
    return (uint16_t)adc;
}

// Inter-sample delay in µs from the current timebase (see header formula).
static uint32_t sampleIntervalUs(uint16_t timebase_us_per_div) {
    uint32_t interval = ((uint32_t)timebase_us_per_div * Theme::GridCols)
                        / SampleBuffers::N;
    if (interval < 1) interval = 1;
    return interval;
}

// --------------------------------------------------------------------------

void Acquisition::begin() {
    analogReadResolution(10);   // 0..1023 raw counts
    _started = false;           // force a fresh acquisition on the first update()
}

void Acquisition::startSearch() {
    _phase       = Phase::Search;
    _searchCount = 0;
    _havePrev    = false;
    _currentTriggered = false;
}

void Acquisition::startSweep() {
    _phase = Phase::Sweep;
    _idx   = 0;
}

void Acquisition::scheduleNext() {
    _nextSampleUs += _interval;
    // If a large real-time gap (redraw blit, menu visit) left us more than one
    // interval behind, drop the backlog rather than firing a compressed burst.
    if ((int32_t)(micros() - _nextSampleUs) > (int32_t)_interval) {
        _nextSampleUs = micros() + _interval;
    }
}

bool Acquisition::paramsChanged(const ScopeState& state, const Settings& settings) const {
    return _sTimebase  != state.timebase_us_per_div
        || _sMode      != state.mode
        || _sSource    != settings.trigSource
        || _sEdge      != settings.trigEdge
        || _sTrigLevel != state.trigger_level_mv;
}

void Acquisition::beginAcq(const ScopeState& state, const Settings& settings) {
    // Snapshot the acquisition-defining params.
    _sTimebase  = state.timebase_us_per_div;
    _sMode      = state.mode;
    _sSource    = settings.trigSource;
    _sEdge      = settings.trigEdge;
    _sTrigLevel = state.trigger_level_mv;

    // Derive per-acquisition constants.
    _interval = sampleIntervalUs(state.timebase_us_per_div);
    _trigAdc  = triggerADC(state.trigger_level_mv);
    _trigPin  = (settings.trigSource == TrigSource::B) ? SIGNAL_B : SIGNAL_A;
    _rising   = (settings.trigEdge == TrigEdge::Rising);

    _nextSampleUs = micros();   // first sample due immediately
    _started      = true;

    if (state.mode == Mode::Triggered) {
        startSearch();
    } else {
        _currentTriggered = true;   // free-running sweeps always "succeed"
        startSweep();
    }
}

bool Acquisition::update(const ScopeState& state, const Settings& settings) {
    if (!_started || paramsChanged(state, settings)) {
        beginAcq(state, settings);
    }

    // Pace: do nothing until the next sample is due.
    const uint32_t now = micros();
    if ((int32_t)(now - _nextSampleUs) < 0) {
        return false;
    }

    if (_phase == Phase::Search) {
        const uint16_t cur = (uint16_t)analogRead(_trigPin);
        scheduleNext();

        if (_havePrev) {
            const bool cross = _rising ? (_prevSample < _trigAdc && cur >= _trigAdc)
                                       : (_prevSample > _trigAdc && cur <= _trigAdc);
            if (cross) {
                _currentTriggered = true;
                startSweep();
                return false;
            }
        }
        _prevSample = cur;
        _havePrev   = true;

        if (++_searchCount >= TRIGGER_SEARCH_SAMPLES) {
            // Window exhausted with no crossing.
            if (settings.trigMode == TrigMode::Auto) {
                _currentTriggered = false;   // free-run this sweep
                startSweep();
            } else {
                // Normal: keep waiting; restart the window, hold the display.
                _searchCount = 0;
                _havePrev    = false;
            }
        }
        return false;
    }

    // Phase::Sweep — read the channels we need into the fill buffer.
    // XY is inherently two-channel; sample both regardless of the display-enable
    // flags (those are a Y-t concept).
    const bool readA = state.channelEnabled[0] || state.mode == Mode::XY;
    const bool readB = state.channelEnabled[1] || state.mode == Mode::XY;
    if (readA) _buf[_fill].ch[0][_idx] = (uint16_t)analogRead(SIGNAL_A);
    if (readB) _buf[_fill].ch[1][_idx] = (uint16_t)analogRead(SIGNAL_B);
    scheduleNext();

    if (++_idx >= SampleBuffers::N) {
        // Frame complete — publish it (swap fill/show) and start the next.
        _buf[_fill].count = SampleBuffers::N;
        _lastTriggered = _currentTriggered;

        const uint8_t tmp = _show;
        _show = _fill;
        _fill = tmp;

        if (_sMode == Mode::Triggered) {
            startSearch();
        } else {
            _currentTriggered = true;
            startSweep();
        }
        return true;   // new frame ready
    }
    return false;
}
