# Acquisition Hardening — Design

**Date:** 2026-07-13
**Branch:** `feature/acq-hardening` (off `main`)
**Status:** Approved design, pending implementation plan

## Problem

A steady input signal does not produce a steady display. Triggered mode (and
to a lesser degree X-Y and Rolling) shows frequent glitches — frames that look
like missed or corrupted reads. The user expectation is simple: a 1 kHz
triangle wave in should be rock-solid on screen.

### Root-cause hypothesis (to be confirmed by measurement)

Acquisition streams both ADCs continuously into double buffers via
`AnalogBufferDMA` (480 samples per buffer). `Acquisition::update()` copies a
completed buffer out during the main loop, but **DMA never stops**: at
500 µs/div a buffer completes every 8 ms, while a full-screen SPI redraw is
likely slower than that. When the render loop trails the DMA, the engine wraps
and overwrites the buffer *while it is being read* — a torn frame. Secondary
suspects: A/B channel pairing races (two independent timers, two interrupt
flags) and the trigger search being limited to the first N samples.

## Goals / success criteria

- With a 1 kHz triangle from an external signal generator, Triggered mode is
  visually rock-steady at every timebase.
- Diagnostics report **zero torn frames** and **zero reader overruns** over a
  5-minute run at each point of the test matrix.
- Channels A and B stay index-paired within ~1 sample.
- Mode-facing behavior is unchanged on this branch. Rolling-timebase extension
  and the X-Y redesign are follow-up branches that build on this one.

## Phase 1 — Characterize

Instrument the **current** acquisition path and measure on hardware before
changing anything.

### Instrumentation (`AcqStats`)

Per-second counters, kept permanently in the codebase (cheap, off by default):

| Stat | Method |
|------|--------|
| Tear events | Checksum the DMA buffer before and after copy-out; mismatch = DMA overwrote it mid-read. Definitive test of the lap hypothesis. |
| Missed buffers | Buffer completion rate vs. consumption rate. |
| Update gap | Max/avg time between successful `update()` calls; render time per frame. |
| A/B skew | Time between the two channels' buffer completions. |
| Trigger misses | Count in Auto/Normal trigger modes. |

### Surfacing

- **USB serial:** full stats line at ~1 Hz.
- **On-screen:** small overlay (tears / fps / buffer rate), toggled from the
  Settings menu ("Diagnostics"), so measurement works untethered at the rack.

### Protocol

Documented in `docs/`, executed with the external generator (1 kHz triangle):
test matrix of timebase {50 µs, 500 µs, 10 ms}/div × mode {Triggered, Rolling,
X-Y}, 5 minutes per cell, results recorded as the baseline table. Phase 2
re-runs the identical matrix for the before/after comparison.

## Phase 2 — Harden: continuous ring capture with write-cursor tracking

Chosen over two alternatives: (A) burst capture — atomic stop/fill/process
bursts, simplest but leaves single-shot gaps and does nothing for slow
rolling; (B) snapshot-on-complete — keeps `AnalogBufferDMA`, shrinks but does
not eliminate the race. The ring design is the only one that also provides the
incremental consumption that slow Rolling (2 s/div) and stable X-Y need.

### Capture side

- Per channel: one DMAMEM ring of **4096 × uint16_t** (8 KB), power-of-two so
  eDMA modulo addressing wraps in hardware. 32-byte aligned (and
  size-aligned as modulo addressing requires).
- Keep the pedvide ADC library for ADC config and timer-paced conversions;
  **drop `AnalogBufferDMA`** and own the eDMA channel: ADC completion
  hardware-triggers a transfer of each result into the ring, continuously.
  No buffer swaps, no interrupt-flag handshake.

### Reader protocol

- **Write cursor:** read from the DMA TCD destination address; a
  major-loop-complete interrupt increments a wrap counter → 64-bit total
  samples written.
- **Read cursor:** Acquisition keeps a 64-bit total consumed. Safe region =
  behind the write cursor minus a guard band of at least one 32-byte dcache
  line (16 samples), so a partially-filled cache line at the write head is
  never read. Reading only the safe region makes torn reads structurally
  impossible.
- **Overrun:** if `written − consumed > ring size`, resync the read cursor to
  the newest safe data and count the event (a stat, never an on-screen glitch).
- **Cache coherency:** ring lives in cached RAM2 — invalidate dcache for
  exactly the region about to be read; the CPU never writes the ring (rule
  established by commit `a59ad4f`). Hardware inversion is undone on copy-out,
  as today.

### Trigger & pairing

- Triggered mode searches the newest safe data for an edge with N samples
  available after it, then copies the aligned window — same concept as today
  but with ≈8.5 buffers of depth and no race pressure.
- A/B timers started back-to-back at the same frequency. Phase 1's skew
  measurement decides whether index-pairing suffices (expected) or tighter
  sync (e.g. ADC_ETC) is needed — a decision point in the plan, not an
  assumption.

### Integration

- `Acquisition::update()` / `frame()` keep their signatures; modes and
  RunScreen are untouched on this branch.
- The ring exposes an incremental read ("all samples since my last read") that
  only the follow-up rolling/X-Y branches consume.
- Bring-up order: ring on channel A alone first, then enable B.

## Risks

- **Hand-rolled eDMA** (TCD config, modulo addressing, ADC↔DMA request IDs on
  i.MX RT1062) is the riskiest piece. Mitigated by keeping Phase 1
  instrumentation live — a mis-configured ring shows up as bad numbers, not
  mystery glitches — and by single-channel bring-up.
- **Timebase extremes:** at very fast timebases sample interval clamps at
  1 µs (1 Msps); ring gives ~4 ms of history there, still ≈4 display windows.

## Testing

- Pure logic — trigger edge search, cursor/window arithmetic, overrun resync —
  extracted into a header of plain functions with a PlatformIO **native-env
  unit test** (cursor off-by-ones are exactly the bugs that read as "random
  glitches" on hardware).
- DMA plumbing validated by re-running the Phase 2 characterization matrix and
  comparing against the Phase 1 baseline.

## Follow-up work (out of scope here, enabled by this design)

1. **Rolling timebase to 2 s/div:** per-mode step table, widen
   `timebase_us_per_div` storage beyond `uint16_t` (65 ms ceiling), consume
   the ring incrementally for smooth sub-frame scrolling.
2. **X-Y redesign:** decouple the figure from the timebase (fixed/adaptive
   sample rate + point accumulation), likely dropping Timebase from X-Y's
   selectable parameters.
