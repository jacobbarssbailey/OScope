// Acquisition.h — Non-blocking ADC acquisition state machine for OScope.
//
// Acquisition samples both input channels (SIGNAL_A / SIGNAL_B) into a
// double-buffered SampleBuffers.  Unlike a blocking sweep, update() does a
// bounded amount of work per call (at most one sample, paced by micros()) and
// returns immediately, so the main loop stays responsive and can service input
// between samples.  A frame is published (made available to frame()) only when
// a full sweep completes; rendering always sees a complete frame.
//
// State machine (per frame):
//   Search  — Triggered mode only: sample the trigger source channel looking
//             for a settings.trigEdge crossing of trigger_level_mv.  On a
//             crossing → Sweep (trigger-aligned).  If the search window is
//             exhausted: Auto → Sweep anyway (free-run); Normal → keep waiting
//             (hold the last frame, no new frame produced).
//   Sweep   — sample both needed channels into the fill buffer until N samples,
//             then publish the frame and start the next one.
// Non-triggered modes (Rolling, XY) skip Search and free-run.
//
// Pacing: each due sample sets the next due time to micros() + interval, so
// samples are spaced by at least `interval` of real time with no catch-up
// bursts (a burst would compress timing after a long gap, e.g. a redraw).
//
// Timebase → interval (µs/sample) = timebase_us_per_div * GridCols / N.
// The acquisition auto-restarts if timebase / mode / trigger source / edge /
// level change, so control edits take effect on the next sample.
#pragma once

#include "modes/ScopeMode.h"
#include "ScopeState.h"
#include "Settings.h"

class Acquisition {
public:
    // Set ADC resolution to 10 bits (0..1023).  Call once in setup().
    void begin();

    // Advance the state machine by a bounded step (non-blocking).  Grabs the
    // sample now due, if any, and progresses the search/sweep.  Returns true
    // exactly once each time a full sweep completes (a new frame is ready).
    bool update(const ScopeState& state, const Settings& settings);

    // Most recently completed frame, for rendering.  count == 0 until the first
    // sweep completes.
    const SampleBuffers& frame() const { return _buf[_show]; }

    // Whether the last completed frame was aligned to a real trigger (true for
    // free-running modes).  Used by single-shot.
    bool lastTriggered() const { return _lastTriggered; }

private:
    enum class Phase : uint8_t { Search, Sweep };

    // Double buffer: one filling, one being shown.
    SampleBuffers _buf[2];
    uint8_t _show = 0;   // index of the last complete frame (rendered)
    uint8_t _fill = 1;   // index currently being written

    // Running state-machine position.
    Phase    _phase       = Phase::Sweep;
    uint16_t _idx         = 0;      // samples written in the current phase
    uint16_t _searchCount = 0;      // trigger-search iterations so far
    uint16_t _prevSample  = 0;      // previous trigger-pin sample (edge detect)
    bool     _havePrev    = false;
    bool     _currentTriggered = false;  // did the in-progress sweep trigger
    bool     _lastTriggered    = false;  // did the last *published* frame trigger
    uint32_t _nextSampleUs = 0;

    // Snapshot of the acquisition-defining params; a change auto-restarts.
    bool       _started    = false;
    uint16_t   _sTimebase  = 0;
    Mode       _sMode      = Mode::Triggered;
    TrigSource _sSource    = TrigSource::A;
    TrigEdge   _sEdge      = TrigEdge::Rising;
    int16_t    _sTrigLevel = 0;

    // Per-acquisition derived constants (recomputed on (re)start).
    uint32_t _interval = 1;
    uint16_t _trigAdc  = 512;
    uint8_t  _trigPin  = 0;
    bool     _rising   = true;

    // (Re)start acquisition from current params; snapshot + derive constants.
    void beginAcq(const ScopeState& state, const Settings& settings);
    // True if any acquisition-defining param differs from the snapshot.
    bool paramsChanged(const ScopeState& state, const Settings& settings) const;

    void startSearch();  // enter Search phase (reset edge-detect state)
    void startSweep();   // enter Sweep phase (reset sample index)

    // Advance _nextSampleUs to the next due time.  Normally steps by one
    // interval on the ideal grid (accurate spacing).  If we have fallen more
    // than one interval behind — e.g. after the ~15 ms redraw blit or a menu
    // visit — it resyncs to now + interval so the backlog can't fire as a
    // timing-compressed burst.
    void scheduleNext();
};
