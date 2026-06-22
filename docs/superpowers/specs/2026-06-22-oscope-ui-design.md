# OScope UI & Interaction Design

Status: approved 2026-06-22
Hardware: Teensy 4.0, 240×240 round GC9A01A display, 1 rotary encoder (+push),
3 buttons (vertical stack, B1 top), 3 LEDs (row), 2 DC input channels (±10 V).

This document defines the control scheme and the software architecture that
supports it. It is the reference for the interaction model; the milestone
breakdown lives in the companion implementation plan.

## 1. Control map

```
        ┌─────────┐     B1 ▢   ← top     Mode / Settings / Back
        │ ENCODER │     B2 ▢             Channel
        │   (●)   │     B3 ▢   ← bottom  Run/Stop
        └─────────┘
   LED1  LED2  LED3   (usage TBD — extra feedback, not yet assigned)
```

### Encoder
- **Turn** → adjust the currently selected parameter.
- **Press (short)** → cycle the selection (context-aware, see §3).
- **Press (long)** → reset scope/acquisition params to defaults (see §5).

### Buttons
- **B1 — Mode / System.** Short: cycle scope mode. Long: enter Settings.
  Inside any menu, B1 means **Back** (top level → exit to run screen).
- **B2 — Channel.** Short: cycle active channel A → B → both. Long: toggle the
  active channel's trace on/off.
- **B3 — Run/Stop.** Short: freeze/run the displayed trace. Long: arm a
  single-shot capture (most meaningful in Triggered mode).

### Press timing (global, consistent across all buttons)
- Hold threshold ≈ **500 ms**.
- **Short press** fires on *release* before the threshold.
- **Long press** fires *once* when the threshold is crossed (while still held).

## 2. Modes

B1 short-press cycles through:

- **Triggered** — classic sweep; waits for the trigger crossing then draws.
- **Rolling** — continuous right-to-left scroll, no trigger. For slow CV/LFO/env.
- **X-Y** — Channel A (X) vs Channel B (Y), Lissajous.

Auto-vs-Normal trigger behavior is a Setting, not a mode. Single-shot is the
Run/Stop long-press, not a mode.

## 3. Context-aware encoder selection

Pressing the encoder steps only through parameters meaningful in the current
mode, so the user never scrolls past dead options:

| Mode      | Encoder cycles through                         | Notes                  |
|-----------|------------------------------------------------|------------------------|
| Triggered | timebase → vertical scale → trigger level      | full set               |
| Rolling   | timebase (scroll speed) → vertical scale       | no trigger             |
| X-Y       | vertical scale                                 | timebase & trigger N/A |

**Vertical scale respects the active channel** (B2): with **A** or **B**
selected it edits that channel only; with **both** it moves both together. In
X-Y both channels are inherently active.

## 4. Settings (B1 long-press)

Navigation inside menus:
- Encoder turn → scroll items.
- Encoder press → enter submenu / begin editing a value (forward).
- B1 → back one level; from top level, exit to run screen.
- B2 / B3 are inactive in menus (keeps the model unambiguous).

Editing a value: press encoder to start editing (item highlights), turn to
change live, press to confirm, B1 to cancel.

Initial contents (intentionally small, designed to grow):
- **Trigger** ▸ Source (A / B), Edge (Rising / Falling), Mode (Auto / Normal)
- **Display** ▸ Brightness, Grid (on / off)
- **About** ▸ firmware version

## 5. Reset to defaults (encoder long-press)

Resets **scope/acquisition params only**: timebase, vertical scale A & B,
trigger level, active channel, mode, run state. Persistent Settings
(brightness, grid, trigger source/edge/mode) are left untouched — a fast "get
me back to a sane trace" action, not a factory wipe.

## 6. Architecture for flexibility

The UI, display style, modes, and settings are all expected to churn during
hardware testing. The architecture isolates each axis of change behind a small
interface so changes stay local.

### 6.1 Input layer — physical → events
A debounced input module samples buttons and the encoder and emits **events**
(`ButtonShortPress`, `ButtonLongPress`, `EncoderDelta`, …) rather than exposing
raw pin state. Physical wiring and debounce live here and nowhere else.
*Change axis: remap/rewire buttons, tweak debounce/long-press timing.*

### 6.2 Screen stack — navigation
A `Screen` interface (`onEnter`, `onExit`, `handleEvent(Event)`, `draw()`) with a
LIFO **stack**: forward = push, Back (B1) = pop. Concrete screens: `RunScreen`,
`MenuScreen`, `EditValueScreen`. Adding UI = implement the interface; menu
forward/back is automatic.
*Change axis: add screens/menus, rearrange navigation.*

### 6.3 Parameter abstraction — the unifying idea
A `Parameter` describes one adjustable value: display name, current value,
step, value→string formatter, and which modes it applies to. Both the encoder
cycle (run screen) and Settings items are lists of `Parameter`s. The encoder
cycle is just "the parameters applicable to the current mode."
*Change axis: add a parameter, reorder the cycle, change steps/formatting —
all data, no control-flow edits.*

### 6.4 Mode strategy — acquisition + render per mode
A `ScopeMode` interface (`acquire(samples)`, `render(renderer, state)`) with one
implementation per mode (`TriggeredMode`, `RollingMode`, `XYMode`). Modes are
registered in a list that B1 cycles. Adding a mode = implement + register;
nothing else changes.
*Change axis: add/replace modes.*

### 6.5 Scope state vs. rendering
`ScopeState` holds acquisition params (timebase, per-channel vscale, trigger
level/source/edge/mode, active channel, run/stop, current mode). Acquisition
fills sample buffers from the ADC. Rendering reads state + samples. Keeping
state separate from drawing means modes and screens share one source of truth.

### 6.6 Theme / renderer — display style in one place
A `Theme` (colors, fonts, layout constants) and a thin `Renderer` wrapper over
the GC9A01A driver. Draw code references theme constants, never magic numbers,
so restyling is a single-file change. Accounts for the round display (keep
content within the safe central band).

### 6.7 Settings persistence
Settings live in a struct with a single `defaults` definition. Persisted to
Teensy EEPROM emulation (later milestone). Reset-to-defaults is a struct copy.

### Summary of change axes → where you touch
| Want to change…            | Edit…                          |
|----------------------------|--------------------------------|
| Button mapping / timing    | input layer (§6.1)             |
| Add a menu / screen        | a new `Screen` (§6.2)          |
| Add/reorder a parameter    | `Parameter` list (§6.3)        |
| Add a scope mode           | a new `ScopeMode` (§6.4)       |
| Restyle the display        | `Theme` (§6.6)                 |
| Add a setting              | `Parameter` + settings struct  |

## 7. Out of scope (for now)
- LED semantics (deferred; extra feedback, undecided).
- Measurements/cursors, FFT, persistence display, dual-trigger.
