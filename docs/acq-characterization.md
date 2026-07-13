# Acquisition Characterization Protocol

Feed a 1 kHz triangle (~±5 V) into channel A (and B where noted). Enable
Settings → Diag. For each cell: set the timebase and mode, watch the display,
and record the serial stats after ~5 minutes (the 1 Hz line prints per-second
deltas and cumulative totals).

Record: tears/s (and cumulative), frames/s, gapmax, trig miss/s, pairwaits/s,
plus a subjective note (steady / occasional jump / frequent glitches).

## Baseline (before ring capture) — commit 1441c01, date 2026-07-13

| Timebase | Triggered | Rolling | X-Y |
|----------|-----------|---------|-----|
| 50 µs/div  | | | |
| 500 µs/div | | | |
| 10 ms/div  | | | |

Notes:

## After ring capture — commit <hash>, date <date>

| Timebase | Triggered | Rolling | X-Y |
|----------|-----------|---------|-----|
| 50 µs/div  | | | |
| 500 µs/div | | | |
| 10 ms/div  | | | |

Notes:

## Acceptance criteria (from the design spec)

- Zero tears and zero overruns in every cell over 5 minutes.
- Triggered display visually rock-steady at every timebase.
- A/B skew within ~1 sample (Phase 2 skew stat).
