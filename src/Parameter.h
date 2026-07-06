// Parameter.h — Descriptor abstraction for encoder-controlled parameters.
//
// Each EncoderParam has a name, an adjust() function that steps the value in
// the ScopeState by a signed encoder detent, and a format() function that
// writes a human-readable string (e.g. "500 us/div") into a caller-supplied
// buffer.  All descriptors live in a static table — no heap allocation.
//
// Key rules:
//   - paramAppliesInMode(): Triggered → all three; Rolling and XY → Timebase +
//     VScale (TriggerLevel has no meaning when free-running).
//   - nextSelectable(): advances selected, skipping params not applicable in
//     the current mode.  VScale is always applicable, so there is always a
//     valid target and the loop cannot run forever.
//   - clampSelectable(): if the current selection is not applicable in the
//     current mode (e.g. after a mode change) move it to the next valid one.
#pragma once

#include "ScopeState.h"
#include <stdint.h>

// Descriptor for one encoder-controlled parameter.
struct Parameter {
    EncoderParam id;
    const char*  name;
    void (*adjust)(ScopeState&, int8_t delta);
    void (*format)(const ScopeState&, char* buf, uint8_t n);
};

// Return the descriptor for a given EncoderParam id.
const Parameter& parameterFor(EncoderParam id);

// True if the parameter is meaningful in the given mode.
//   Triggered: Timebase, VScale, TriggerLevel
//   Rolling:   Timebase, VScale
//   XY:        Timebase, VScale
bool paramAppliesInMode(EncoderParam id, Mode m);

// Return the next selectable EncoderParam after s.selected, skipping any that
// do not apply in s.mode.  Never modifies s; just returns the new value.
EncoderParam nextSelectable(const ScopeState& s);

// If s.selected is not applicable in s.mode, advance it until it is.
// Call this whenever s.mode changes.
void clampSelectable(ScopeState& s);
