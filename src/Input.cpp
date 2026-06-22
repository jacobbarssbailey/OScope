// Input.cpp — Implementation of the Input event layer.
// See Input.h for the public interface and event semantics.

#include "Input.h"
#include "Config.h"
#include <Encoder.h>

// Pin table must match the Btn enum order: Mode, Channel, RunStop, Encoder.
static const uint8_t BTN_PIN[] = {BTN_MODE, BTN_CHAN, BTN_RUN, SW_ENC};

// Encoder object (uses interrupt-driven quadrature decoding from the library).
// Pins ordered B,A (not A,B) so CW rotation reads positive on this hardware.
static Encoder s_encoder(ENC_B, ENC_A);
static long s_lastEnc = 0;

// ---------------------------------------------------------------------------
// Internal FIFO helpers (operate on member arrays via file-scope functions).
// The queue is kept in the Input instance so multiple Input objects could
// coexist without colliding; the encoder/BtnState are module-level singletons
// because there is exactly one physical encoder.
// ---------------------------------------------------------------------------

static_assert(Input::QUEUE_SIZE && !(Input::QUEUE_SIZE & (Input::QUEUE_SIZE - 1)),
              "QUEUE_SIZE must be a power of two");

static void qpush(InputEvent* q, uint8_t& head, uint8_t& tail,
                  const InputEvent& e) {
    uint8_t next = (tail + 1) & (Input::QUEUE_SIZE - 1);
    if (next != head) {  // drop silently when full (shouldn't happen at 8 deep)
        q[tail] = e;
        tail    = next;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Input::begin() {
    for (uint8_t i = 0; i < (uint8_t)Btn::COUNT; i++) {
        pinMode(BTN_PIN[i], INPUT_PULLUP);
    }
    s_lastEnc = s_encoder.read();
}

bool Input::poll(InputEvent& out) {
    uint32_t now = millis();

    // -- Encoder: accumulate raw counts, emit one EncoderTurn per detent (4 counts). --
    long rawPos = s_encoder.read();
    long pos    = rawPos / 4;
    long start  = s_lastEnc / 4;
    if (pos != start) {
        InputEvent e;
        e.type  = EventType::EncoderTurn;
        e.delta = (int8_t)(pos - start);
        qpush(_queue, _head, _tail, e);
        s_lastEnc = rawPos;  // baseline is the SAME read that produced the delta
    }

    // -- Buttons: debounce (10 ms settle) + short-on-release / long-at-threshold. --
    for (uint8_t i = 0; i < (uint8_t)Btn::COUNT; i++) {
        bool      raw = (digitalRead(BTN_PIN[i]) == LOW);  // active-low
        BtnState& b   = _btn[i];

        // Detect raw edge and record time.
        if (raw != b.rawLast) {
            b.rawLast = raw;
            b.change  = now;
        }

        if (now - b.change >= 10) {  // debounce settle window
            if (raw && !b.stable) {
                // Rising edge (press): record press time, clear long-fire flag.
                b.stable    = true;
                b.downAt    = now;
                b.longFired = false;
            } else if (!raw && b.stable) {
                // Falling edge (release): emit ShortPress if long hasn't fired.
                b.stable = false;
                if (!b.longFired) {
                    InputEvent e;
                    e.type   = EventType::ShortPress;
                    e.button = (Btn)i;
                    qpush(_queue, _head, _tail, e);
                }
            }
        }

        // Long-press: fire once when held past threshold (before release).
        if (b.stable && !b.longFired && (now - b.downAt) >= LONG_PRESS_MS) {
            b.longFired = true;
            InputEvent e;
            e.type   = EventType::LongPress;
            e.button = (Btn)i;
            qpush(_queue, _head, _tail, e);
        }
    }

    // Pop the oldest queued event, if any.
    if (_head == _tail) return false;
    out   = _queue[_head];
    _head = (_head + 1) & (Input::QUEUE_SIZE - 1);
    return true;
}
