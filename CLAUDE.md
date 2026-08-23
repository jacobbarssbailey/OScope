# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for a Teensy 4.0 dual-channel oscilloscope Eurorack module with a 240×240 round
GC9A01A display, a rotary encoder, and three buttons. Pin assignments live in `src/Config.h`
and are mirrored in README.md's pin map — change both together.

## Commands

```bash
just build                 # pio run — compile the firmware
just run                   # pio run -t upload — build + flash the Teensy
just debug                 # upload, then open the serial monitor
just test                  # pio test -e native — AcqCore unit tests on the host
pio test -e native -f test_acqcore   # run one test suite
just preview [screen…]     # render UI previews to tools/preview/out
```

Only `lib/AcqCore/` is host-testable (`[env:native]`); everything else needs the Teensy
toolchain. `pio run` is fast (~1 s incremental) — build after every change rather than
batching.

## Architecture

**Main loop** (`src/OScope.ino`) owns the framebuffer and is the only code that hands a
frame to the panel. Each iteration it drains input events, calls `screens.tick()`, and
redraws *only* when a handled event or a completed acquisition frame says something changed.

The transfer is ~19 ms (240·240·16 bits at 48 MHz) against ~0.5 ms of drawing, so it *is*
the frame budget. `DISPLAY_ASYNC_BLIT` hands it to DMA and lets the loop keep polling input
and servicing acquisition instead of blocking — worth 20 ms → 1 ms on the acquisition
service gap. There is one framebuffer, so the next draw is gated on `asyncUpdateActive()`;
drawing during a transfer would tear. **Do not add double buffering without reading
`docs/display-async.md` first** — calling `setFrameBuffer()` per frame leaks a DMA channel
per frame via `DMAChannel::begin(true)` and hardfaults the board in about eleven frames.

**Layered structure**, each layer knowing only the one below:

- `Input` — debounces the buttons/encoder into a queue of `InputEvent`s (ShortPress,
  LongPress, EncoderTurn).
- `Screen` / `ScreenStack` (`src/screens/Screen.h`) — fixed depth-6 stack, no heap.
  RunScreen is the root; MenuScreen and EditValueScreen are pushed on demand. Every screen
  method takes `AppContext` (state + stack + settings).
- `ScopeMode` (`src/modes/ScopeMode.h`) — strategy per acquisition/display mode (Triggered,
  Rolling, XY, Spectrum, Tuner, Waterfall). RunScreen holds one instance of each and
  dispatches through `_modes[]`, indexed by the `Mode` enum. `render()` must be a pure
  function of (state, buffers); cross-frame history is accumulated in `onFrame()`, which is
  called exactly once per completed sweep.
- `Renderer` — the only drawing API screens use. It does not own the framebuffer and never
  blits. Modes that need raw pixel access go through `r.tft.getFrameBuffer()`.

**Two sources of persisted truth**, both in emulated EEPROM at separate addresses:
`ScopeState` (live acquisition setup — mode, timebase, V/div, trigger level; reset by an
encoder long-press) and `Settings` (user configuration reached through the menu). Both are
saved debounced, not on every detent. Bump the version byte (`kVersion` in `Settings.cpp`,
`kStateVersion` in `ScopeState.cpp`) whenever the stored layout changes, or a stale record
will load as garbage.

**Acquisition** (`src/Acquisition.*`, `src/RingCapture.*`) is timer-paced ADC → eDMA into a
per-channel 4096-sample ring, with no CPU in the path. Readers trail the hardware write
cursor behind a guard band, which makes torn reads structurally impossible rather than
merely detectable — see `docs/superpowers/specs/2026-07-13-acquisition-hardening-design.md`
and `docs/acq-characterization.md` for the bench protocol. **The CPU never writes the
rings** (cache coherency). Triggering is done in software over a linear scratch copy.

Two acquisition paths exist and must stay separate: `update()` is the trigger-aligned,
sweep-paced path (Triggered only), and `updateFreeRunning()` publishes the newest window
whenever samples arrive (Rolling, XY, Spectrum, Tuner, Waterfall) so their frame rate is
blit-bound rather than sample-bound. Spectrum/Tuner/Waterfall additionally pull their own
wider analysis blocks straight from the rings via `readNewestBlock()`.

`lib/AcqCore/AcqCore.h` holds the hardware-free logic (trigger search, ring cursor
arithmetic, YIN pitch detection) precisely so it can be unit-tested off-target. Put new
acquisition logic there when it does not need hardware.

**Parameter tables.** `Parameter` (encoder-adjustable acquisition params) and `SettingItem`
(menu settings) are parallel descriptor tables of name + adjust + format function pointers,
driving the UI generically. Both `format()` functions write the value and its unit as
*separate* strings, because readouts set them at different sizes on a shared baseline.
`paramAppliesInMode()` gates which params exist per mode; call `clampSelectable()` after any
mode change.

## Design system

`src/Theme.h` is the single source of truth for every colour and layout pixel — a file that
hard-codes a colour hex or a screen coordinate should reference a constant there instead.
Colours are RGB565; coordinates assume 240×240 at rotation 2.

Type is Inter Bold Italic at four sizes (`src/Fonts.h` → `src/font_Inter.cpp`), referenced
through semantic aliases (`FONT_BODY`, not `Inter_20_Bold_Italic`). The t3 font renderer
treats the cursor Y as the **top of the cap height**, so `y` positions a capital's top edge
and `Renderer::textUnit()` uses the cap-height difference to sit a smaller unit on the same
baseline. `src/Icons.*` holds generated 8-bit coverage masks for the button glyphs; tinting
one white reproduces the source art exactly.

Two aids for checking layout against design mockups:

- `just preview` (`tools/preview/`) rasterises the real glyph data on the host and renders
  each screen to a PNG, so positions can be checked without a flash cycle. It scrapes the
  layout constants out of the C++ sources, but each screen's `draw()` is transcribed by
  hand — change a screen's structure and the preview needs the same change.
- `UI_DEBUG_GRID` in `src/Config.h`, set to 1, overlays a 16 px ruler on the device itself.

## Conventions

- No dynamic allocation anywhere: every object is statically allocated by the caller.
- Comments explain *why* a decision was made, not what the line does — match that register.
  Several constants encode hard-won bench results; don't "simplify" them without reading the
  surrounding comment.
- `ACQ_DIAG` in `src/Config.h` compiles in the 1 Hz `acq:` characterization logging. It is a
  bench tool, off in shipping builds.
