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
| **Channel** (B2) | the current mode's own option | — | — |
| **Run/Stop** (B3) | freeze / resume | arm single shot | — |
| **Encoder** | select the next setting | reset everything to defaults | change the selected setting |

Two of those change meaning with the mode.

**Channel** is the per-mode option key: whatever single thing the current mode
has worth switching, it switches. Persistence in Triggered and X-Y, peak hold in
Spectrum, flow direction in Waterfall. It has no hold, and nothing on it in
Rolling or Tuner yet.

The **encoder press** normally walks the settings, but Spectrum and Tuner have
none, so there it takes the mode's own value instead: the spectrum layout, or
hertz-or-note.

### Every control in every mode

Every cell is spelled out — a dash means the control does nothing in that mode.

| | TRIG | ROLL | X-Y | SPEC | TUNE | WFAL |
|---|---|---|---|---|---|---|
| **Encoder turn** | change setting | change setting | change setting | bucket count | — | — |
| **Encoder press** | next setting (of 3) | next setting (of 2) | next setting (of 2) | spectrum layout | Hz ↔ note | — |
| **Encoder hold** | reset settings | reset settings | reset settings | reset settings | reset settings | reset settings |
| **Mode press** | next mode | next mode | next mode | next mode | next mode | next mode |
| **Mode hold** | settings menu | settings menu | settings menu | settings menu | settings menu | settings menu |
| **Channel press** | persistence on/off | — | persistence on/off | peak hold on/off | — | flow direction |
| **Channel hold** | — | — | — | — | — | — |
| **Run/Stop press** | freeze/resume | freeze/resume | freeze/resume | freeze/resume | freeze/resume | freeze/resume |
| **Run/Stop hold** | arm single shot | — | — | — | — | — |

Five of those nine depend on the mode. Three more — next mode, settings menu,
freeze — plus reset do the same thing wherever you are, and Channel hold is
unused everywhere. Three rows are worth reading twice:

- **Channel press** is the option key, and what it offers depends on what the
  mode has. Two modes have nothing on it yet: Rolling, because persistence
  would only smear a display that already scrolls, and Tuner, which is an open
  question — see [Known limitations](#current-limitations).
- **Encoder press** walks the settings where there are settings, and otherwise
  becomes the mode's own value. Waterfall has neither, so it does nothing there.
- **Run/Stop hold** arms a single shot, which completes on the next *triggered*
  capture. Only Triggered produces one, so the hold is ignored in the other
  modes rather than arming something that could never fire.

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

**Volts** runs 50 mV to 5 V per division in twelve steps, and always moves both
channels together — hiding channel B does not take it out of the scaling, so it
comes back at the scale you would expect. The face is eight divisions tall, so
at the 3 V/div default the visible window is ±12 V — wider than the ±10 V inputs
can reach.

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

Press **Mode** to cycle. Nothing announces the change — each mode is
unmistakable from what it draws, and a label would cover the thing you switched
modes to look at.

**TRIG — Triggered.** The conventional oscilloscope view. Each sweep is aligned
to the point where the signal crosses the trigger level, so a repeating waveform
stands still. The edge it triggers on — rising or falling — is in the settings
menu.

**ROLL — Rolling.** No trigger: the trace scrolls continuously right to left,
new samples entering at the right edge. Useful for slow signals like envelopes
and LFOs, where waiting for a trigger would just mean waiting.

**X-Y — Lissajous.** Channel A drives the horizontal axis and channel B the
vertical, so two related signals draw a figure instead of two traces. Volts
scales both axes.

**SPEC — Spectrum.** A 256-point FFT of each channel, with more controls on it
than any other mode. The frequency window and amplitude mapping are fixed, but
three things are not:

- **Turn the encoder** for the bucket count: 128, 64 or 32. The block keeps its
  width, so the bars widen as they thin out — 1, 2 or 4 px. Fewer buckets
  average more FFT bins together rather than dropping the ones in between, so a
  coarse view is genuinely coarser and not just sparser.
- **Press the encoder** for the layout. *Bars* is the classic reading: A up and
  B down from a centre line. The two circular layouts give each channel a half
  of the face — A on the left, B on the right, low frequency at the top of both
  — with the buckets as spokes growing *outward* from a hub or *inward* from
  the rim.
- **Press Channel** for peak hold: each bucket's recent maximum, held for about
  three quarters of a second and then falling, drawn clear of its bar so a
  transient stays readable after the bar has dropped.

**TUNE — Tuner.** Pitch detection on both channels, A in the top half of the
face and B in the bottom. **Press the encoder** to switch the readout between
frequency in hertz and musical note; in note mode a bar either side of centre
shows how sharp or flat each channel is. The A4 reference is in the settings
menu.

**WFAL — Waterfall.** A scrolling spectrogram — each FFT frame becomes a line of
colour and the lines scroll over time, so you watch the spectrum evolve.
Channel A owns the left half of the face, channel B the right. **Press
Channel** to switch the flow direction:

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
freezes on it and disarms.

Since it waits for a trigger, this is a Triggered-mode tool — nothing else
produces a trigger-aligned capture, so holding Run/Stop in the other modes does
nothing at all rather than arming something that could never fire. A press of
Run/Stop cancels a pending single shot.

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

**Press Channel** in Triggered or X-Y to switch persistence off and on without
opening the menu. It remembers the level the menu is set to, so switching back
on restores *short*, *med* or *long* rather than jumping to a default.

**Persist** leaves a phosphor-like trail behind the trace, which makes jitter
and modulation visible as a band rather than a flicker. It applies to Triggered
and X-Y only — Rolling already shows time as a scroll, and the FFT modes manage
their own display. It defaults to **long**.

### How long is a trail, in seconds?

The trail is measured in **frames**, not seconds: each frame the untouched
pixels are multiplied down, and a trace pixel takes a fixed number of frames to
reach black.

| | fade per frame | frames to black | at 32 fps | at 6.2 fps |
|---|---|---|---|---|
| **short** | ×0.59 | 6 | 0.19 s | 0.97 s |
| **med** | ×0.78 | 10 | 0.31 s | 1.6 s |
| **long** | ×0.91 | 18 | 0.56 s | 2.9 s |

So the same setting gives a visibly longer trail at slow timebases. The two
columns are the real ends of the range: the display blit caps the frame rate at
about **32 fps**, which is what you get in X-Y and in Triggered at most
timebases, while Triggered at 10 ms/div is sweep-bound and drops to about
**6.2 fps**. In between, scale accordingly.

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

- **The Channel button does nothing in Rolling or Tuner.** Rolling has no
  persistence to switch — a trail would only smear a display that already shows
  time as a scroll — and Tuner has not been given an option yet. The
  candidates for Tuner are a hold on the last confident reading, so a plucked
  string can be read after it decays; a fine/coarse cents scale; or transpose.
- **Neither channel can be hidden.** Both traces always draw. The controls for
  hiding one were removed when the Channel button was given over to per-mode
  options.
- **The three panel LEDs do nothing.** They are wired and assigned pins, but no
  firmware drives them yet.
- **There is no way to scale one channel independently.** Volts moves both
  together by design. The per-channel plumbing exists underneath — each channel
  stores its own V/div — but nothing surfaces a way to set them apart.
- **The spectrum layout, bucket count and peak-hold state are not saved.** They
  are the mode's own, like the tuner's readout and the waterfall's flow
  direction, and none of those survive a power cycle. Only the acquisition setup
  and the menu settings do.
- **Spectrum, Tuner and Waterfall have no adjustable acquisition settings.**
  Their sample rates, frequency windows and scaling are fixed at bench-tuned
  values.

---

## Quick reference

```
Mode  (B1)   press  next mode: TRIG > ROLL > X-Y > SPEC > TUNE > WFAL
             hold   settings menu       (in a menu: press = back / cancel)

Chan  (B2)   press  the mode's own option:
                    TRIG / X-Y: persistence on-off
                    SPEC: peak hold on-off.  WFAL: flow direction
                    ROLL, TUNE: nothing yet
             hold   nothing

Run   (B3)   press  freeze / resume     (solid orange ring = frozen)
             hold   arm single shot     (dashed ring = armed; TRIG only)

Enc          press  next setting        (in a menu: open / confirm)
                    SPEC: spectrum layout.  TUNE: Hz <-> note
             hold   reset acquisition settings to defaults
             turn   change the selected setting
                    SPEC: bucket count (128 / 64 / 32)
```
