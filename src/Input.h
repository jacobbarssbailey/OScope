// Input.h — Debounced button and encoder event layer.
//
// The Input class samples the four buttons (Mode, Channel, RunStop, Encoder)
// and the rotary encoder on every poll() call and queues discrete InputEvents:
//   - ShortPress  fires on release when held < LONG_PRESS_MS
//   - LongPress   fires once when held >= LONG_PRESS_MS (before release)
//   - EncoderTurn fires once per accumulated detent of rotation
//
// Callers drain the queue with poll() in a loop; it returns false when empty.
// No dynamic allocation is used: all state is static/stack-based.
#pragma once

#include <Arduino.h>

enum class Btn : uint8_t { Mode, Channel, RunStop, Encoder, COUNT };
enum class EventType : uint8_t { None, ShortPress, LongPress, EncoderTurn };

struct InputEvent {
    EventType type   = EventType::None;
    Btn       button = Btn::Mode;  // valid for ShortPress / LongPress
    int8_t    delta  = 0;          // valid for EncoderTurn (signed detents)
};

class Input {
public:
    void begin();
    // Pops the next pending event; returns false when none remain.
    bool poll(InputEvent& out);

private:
    // Fixed-size event FIFO (power-of-2 size for cheap modulo).
    static const uint8_t QUEUE_SIZE = 8;
    InputEvent _queue[QUEUE_SIZE];
    uint8_t    _head = 0;
    uint8_t    _tail = 0;

    // Per-button debounce + long-press state.
    struct BtnState {
        bool     rawLast   = false;
        bool     stable    = false;
        uint32_t change    = 0;  // millis() of last raw edge
        bool     longFired = false;
        uint32_t downAt    = 0;  // millis() when button was pressed (stable)
    };
    BtnState _btn[(int)Btn::COUNT];
};
