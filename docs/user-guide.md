# OScope — User Guide

A dual-channel oscilloscope in a Eurorack module: two ±10 V inputs, a 240×240
round display, three buttons and a rotary encoder. Six display modes, no menus
in the signal path — the face shows the waveform and nothing else until you
touch a control.

> The physical arrangement of the three buttons on the panel is not described
> here yet. Throughout this guide they are named by function — **Mode**,
> **Channel** and **Run/Stop** — which is the order the firmware knows them in
> (B1, B2, B3).

---

## The controls at a glance

Every control does one thing on a short press and a different thing when held
for half a second. Nothing is modal: the same press means the same thing in
every mode.

| Control | Short press | Hold (0.5 s) | Turn |
|---|---|---|---|
| **Mode** (B1) | next display mode | open the settings menu | — |
| **Channel** (B2) | mode-specific (see below) | show/hide channel A's trace | — |
| **Run/Stop** (B3) | freeze / resume | arm single shot | — |
| **Encoder** | select the next setting | reset everything to defaults | change the selected setting |

The **Channel** short press does nothing in most modes. Only Waterfall claims
it, where it flips the scroll direction.

---

## Adjusting the acquisition settings

The settings are not on screen while you are looking at a signal. Press or turn
the encoder and the whole face dims behind a scrim, listing every setting that
applies in the current mode:

- **Timebase** (clock icon) — time per division
- **Volts** (waveform icon) — vertical scale per division
- **Trigger** (level icon) — the level the sweep triggers on

The one you are editing is white; the others are grey. **Press** the encoder to
move to the next one, **turn** it to change the value. The overlay clears itself
two seconds after you stop, and the trace comes back to full brightness.

Modes that have nothing to adjust — Spectrum, Tuner and Waterfall — raise no
overlay at all.

### What the values mean

**Timebase** is per mode, and each mode remembers its own. Switching modes
restores what you last set there.

| Mode | Range |
|---|---|
| Triggered | 50 µs – 10 ms per division |
| Rolling | 500 µs – 1 s per division |
| X-Y | 500 µs – 100 ms per division |
| Spectrum / Tuner / Waterfall | fixed, not adjustable |

Steps follow a 1‑1.5‑2‑3‑5‑7 sequence per decade, so one detent is a small,
even change rather than a jump to the next decade.

**Volts** runs 50 mV to 5 V per division in twelve steps, and moves both
channels together. The face is eight divisions tall, so at the 3 V/div default
the visible window is ±12 V — wider than the ±10 V inputs can reach.

**Trigger** moves in proportion to what is on screen rather than in a fixed
step: one detent is a fifth of a division, and the range is the visible
half-screen (±4 divisions), capped at the ±10 V input limit. That means the
level always crosses the trace within a sane number of clicks, whatever the
vertical scale. It reads in millivolts below 1 V and in volts above it, so a
level at the top of the range shows as `-10V` rather than `-10000mV`.

Push the level past the top or bottom of the signal and triggering stops, which
is the expected way to check where the trace actually reaches.

---

## The six modes

Press **Mode** to cycle. The mode's name flashes in the middle of the face for a
moment and then gets out of the way.

**TRIG — Triggered.** The conventional oscilloscope view. Each sweep is aligned
to the point where the signal crosses the trigger level, so a repeating waveform
stands still. The edge it triggers on — rising or falling — is in the settings
menu.

**ROLL — Rolling.** No trigger: the trace scrolls continuously right to left,
new samples entering at the right edge. Useful for slow signals like envelopes
and LFOs, where waiting for a trigger would just mean waiting.

**X-Y — Lissajous.** Channel A drives the horizontal axis and channel B the
vertical, so two related signals draw a figure instead of two traces. Volts
scales both axes. X-Y always samples both channels regardless of what is hidden.

**SPEC — Spectrum.** A 256-point FFT of each channel drawn as a 128-bucket bar
spectrum: channel A grows up from the centre line, channel B grows down. The
frequency window and amplitude mapping are fixed.

**TUNE — Tuner.** Pitch detection on both channels, A in the top half of the
face and B in the bottom. **Turn the encoder** to switch the readout between
frequency in hertz and musical note; in note mode a bar either side of centre
shows how sharp or flat each channel is. The A4 reference is in the settings
menu.

**WFAL — Waterfall.** A scrolling spectrogram — each FFT frame becomes a line of
colour and the lines scroll over time, so you watch the spectrum evolve.
Channel A owns the left half of the face, channel B the right. **Press Channel**
to switch the flow direction:

- *Up* — frequency across the face, time scrolling upward from the bottom.
- *Out* — frequency up the face, time scrolling outward from the centre.

Switching direction clears the history.

---

## Freezing the display

**Press Run/Stop** to freeze. The last frame stays on screen and a solid orange
ring appears around the bezel. Press again to resume. You can still change the
settings while frozen — the readouts update even though the trace does not.

**Hold Run/Stop** to arm a single shot: the ring turns to a dashed orange
outline, the scope runs until the next successful triggered capture, then
freezes on it and disarms. Since it waits for a trigger, this is a Triggered-mode
tool. A manual press of Run/Stop cancels a pending single shot.

---

## The settings menu

**Hold Mode** to open it. The list straddles the centre of the face, names on
the left and values on the right, with the highlighted row picked out in pink.

- **Turn the encoder** to move down the list (it wraps).
- **Press the encoder** to open the highlighted setting.
- **Press Mode** to go back to the scope.

Inside a setting: turn the encoder to change the value live, **press the
encoder to confirm and save**, or **press Mode to cancel** and put back what was
there before.

| Setting | Values | What it does |
|---|---|---|
| **edge** | rising / falling | which direction the trigger fires on, in Triggered mode |
| **A4** | 400 – 480 Hz | the tuner's reference pitch |
| **persist** | off / short / med / long | how long a trace lingers before fading |

**Persist** leaves a phosphor-like trail behind the trace, which makes jitter
and modulation visible as a band rather than a flicker. It applies to Triggered
and X-Y only — Rolling already shows time as a scroll, and the FFT modes manage
their own display.

---

## What the module remembers

Almost everything survives a power cycle. The acquisition setup — mode, all six
timebases, volts per division, trigger level, which channels are showing — is
written to internal memory a couple of seconds after you stop changing it, so a
burst of encoder turns costs one write rather than twenty. The menu settings
save when you confirm them.

Two things are deliberately not remembered: the scope always boots **running**,
and always boots with single shot **disarmed**.

**Hold the encoder** to put every acquisition setting back to its default. This
does not touch the settings menu — edge, A4 and persist keep their values.

---

## Current limitations

Honest notes about the interface as it stands, so nothing reads as a fault:

- **Hiding a trace only works on channel A.** Holding Channel toggles the
  focused channel, and the focus is currently pinned to A+B with A as the lead,
  so channel B cannot be hidden. Related: with A hidden, a Volts edit moves
  channel B's scale while the readout still shows A's.
- **The three panel LEDs do nothing.** They are wired and assigned pins, but no
  firmware drives them yet.
- **Channel selection is not exposed.** The scope always acquires and displays
  both channels; the per-channel plumbing exists but the v2 interface does not
  surface a way to focus one.
- **Spectrum, Tuner and Waterfall have no adjustable acquisition settings.**
  Their sample rates, frequency windows and scaling are fixed at bench-tuned
  values.

---

## Quick reference

```
Mode  (B1)   press  next mode: TRIG > ROLL > X-Y > SPEC > TUNE > WFAL
             hold   settings menu       (in a menu: press = back / cancel)

Chan  (B2)   press  Waterfall: flow direction; other modes: nothing
             hold   show/hide channel A's trace

Run   (B3)   press  freeze / resume     (solid orange ring = frozen)
             hold   arm single shot     (dashed ring = armed)

Enc          press  next setting        (in a menu: open / confirm)
             hold   reset acquisition settings to defaults
             turn   change the selected setting
                    Tuner: switch the readout between Hz and note
```
