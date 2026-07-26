# Acquisition Characterization Protocol

Feed a 1 kHz triangle (~±5 V) into channel A and channel B. Build with
`ACQ_DIAG 1` in `src/Config.h`. Then walk the 3 × 3 grid of timebases and modes
below, holding each combination steady for **2.5 minutes (150 s)**. Nine cells,
so about 23 minutes of dwell plus knob time.

The firmware does the aggregation: hold a cell steady and it prints one `cell:`
line per cell with every number the table wants. Nothing to postprocess.

## What the firmware prints

A "cell" is one (timebase, mode) pair. Changing either one starts a new cell and
resets the accumulators, so a knob sweep through intermediate timebases never
pollutes a dwell. No reboot needed between cells.

Once per second, the live line:

```
acq: [50 us/div TRIG] t=92s fps=28 bufs=2083 tears=7(608) miss=141 pairwait=1329 over=0 gap=51ms skew=1
```

`t` is seconds into the current cell. `tears=7(608)` is this second's delta and
the cell total. `bufs` is the DMA buffer completion rate at this timebase, which
is *not* an achievable frame rate: at short timebases buffers complete far faster
than the display can consume them (a full-frame blit at 30 MHz SPI costs ~31 ms,
capping fps near 32). `fps` well below `bufs` is the tear mechanism, not a fault
in the reader.

Every 30 s, and once at 150 s, the aggregate line:

```
cell: [50 us/div TRIG] t=150/150s DONE fps=28.2 tears=608(6.6/s avg,10/s pk) miss=21150(178/s pk) pairwait=199500(1585/s pk) over=0 gapmax=81ms skew=1 pk FAIL:tears
```

That line **is** the table row. `DONE` means the dwell is complete, so move the
knob. Checkpoints before that are tagged `...`. If a cell is cut short (knob
moved before 150 s, but after at least 10 s) it prints `CUT SHORT` so the log
says why a row is missing rather than silently dropping it. Sweeps shorter than
10 s are silent.

`PASS` / `FAIL` on the `cell:` line checks the machine-checkable acceptance
criteria: zero tears, zero overruns, and A/B skew within 2 samples. Trigger
misses are *expected* at short timebases, where a 480-sample buffer spans less
than one input period, and do not fail a cell.

## Procedure

1. Set `ACQ_DIAG 1` in `src/Config.h`. Diagnostics are compile-time rather than
   a menu toggle: it is a bench tool, not a user feature, so there is nothing to
   leave switched on by accident and the reporting drops out of shipping builds.
2. `just debug` (uploads, then opens the serial monitor at 115200). Pipe to a
   file if you want to keep the log: `just debug | tee docs/acq-<label>.log`.
3. Leave the run screen up and running: the reporter is not called while the
   display is frozen or while you are in the settings menu, and a gap longer
   than 2 s is discarded rather than recorded as one very busy second.
4. For each of the nine cells: set the timebase and mode, wait for `DONE`, paste
   the `cell:` line into the table below, and add a subjective note (steady /
   occasional jump / frequent glitches).
5. Set `ACQ_DIAG` back to 0 before shipping.

## Baseline (before ring capture) — commit 46f8852, date 2026-07-25

Full run: all nine cells to 150 s, no cell cut short. Raw log:
`docs/acq-baseline-001.log`. Per cell: verdict, tears total (avg/s, peak/s),
mean fps, worst gap.

| Timebase | Triggered | Rolling | X-Y |
|----------|-----------|---------|-----|
| 50 µs/div  | **FAIL** 777 tears (5.1/s, 9/s pk)<br>fps 29.5, gap 51 ms | **FAIL** 167 tears (1.1/s, 4/s pk)<br>fps 31.8, gap 125 ms | **FAIL** 181 tears (1.2/s, 5/s pk)<br>fps 32.0, gap 32 ms |
| 500 µs/div | **FAIL** 168 tears (1.1/s, 5/s pk)<br>fps 32.0, gap 31 ms | **FAIL** 161 tears (1.0/s, 4/s pk)<br>fps 32.0, gap 31 ms | **FAIL** 393 tears (2.6/s, 6/s pk)<br>fps 32.0, gap 31 ms |
| 10 ms/div  | **PASS** 0 tears<br>fps 6.2, gap 159 ms | **PASS** 0 tears<br>fps 6.2, gap 159 ms | **PASS** 0 tears<br>fps 6.2, gap 161 ms |

Trigger misses (Triggered mode only): 15508 total, 170/s peak at 50 µs/div;
zero at 500 µs/div and 10 ms/div, as expected once the 480-sample buffer spans
more than one input period.

Notes:

- **Zero overruns in all nine cells** (1578 report windows). Criterion 1 fails
  only on the tear half.
- **Six of nine cells tear.** Only 10 ms/div is clean, in every mode. That is
  the timebase where buffers complete at ~6/s, far below the ~32 fps blit
  ceiling, so the DMA never laps the copy. Tearing tracks the
  buffers-per-frame ratio, which is exactly what ring capture removes.
- **Worst cell is 50 µs/div Triggered** at 5.1 tears/s, ~4x any other cell.
  Triggered does the most work per buffer (trigger search over the first N
  samples) at the timebase where buffers complete fastest.
- **X-Y at 500 µs/div tears ~2.4x** the Triggered and Rolling cells at the same
  timebase (2.6/s versus 1.0-1.1/s), so the mode's per-frame cost matters, not
  just the timebase.
- **Rolling at 50 µs/div shows a 125 ms worst gap** against a 31 ms mean frame
  period, so it stalls for ~4 frame times occasionally. The other 50 µs cells
  stay at 32-51 ms. Worth a look independent of ring capture.
- `pairwait` confirms it is a poll-count artifact, not a health signal: 10 ms/div
  reads 3605/s peak in Triggered but 485413/s in Rolling and X-Y, tracking loop
  speed and mode rather than capture behavior.
- Subjective per-cell steadiness notes were not recorded during this run.

## After ring capture — commit f4d64fd, date 2026-07-25

Full run, all nine cells to 150 s. Raw log: `docs/acq-phase2-001.log`.

| Timebase | Triggered | Rolling | X-Y |
|----------|-----------|---------|-----|
| 50 µs/div  | 0 tears<br>fps 26.1, gap 71 ms, skew 2 | 0 tears<br>fps 32.6, gap 31 ms, **skew 80** | 0 tears<br>fps 33.0, gap 31 ms, **skew 80** |
| 500 µs/div | 0 tears<br>fps 32.0, gap 31 ms, skew 1 | 0 tears<br>fps 32.0, gap 31 ms, skew 2 | 0 tears<br>fps 32.0, gap 31 ms, **skew 78** |
| 10 ms/div  | 0 tears<br>fps 6.2, gap 159 ms, skew 1 | 0 tears<br>fps 6.2, gap 191 ms, skew 1 | 0 tears<br>fps 6.2, gap 159 ms, **skew 78** |

**Criterion 1 passes everywhere: zero tears and zero overruns in all nine
cells**, against six of nine cells tearing in the baseline. Trailing the write
cursor removed tearing as designed.

**Criterion 3 failed in four cells at the time of this run — since fixed.** It
was a pairing bug rather than a property of ring capture. Root cause and fix:
`d8dbe54`, with the raw investigation in `docs/acq-skew-probe.log` and
confirmation in `docs/acq-skew-verify.log` (max skew 1 across 89 readings,
including 60 at 150 µs/div and 20 at 100 µs/div, both of which used to latch to
30 and 52).

**This table therefore predates the fix and is worth re-running on `d8dbe54`**
for a clean record: the tears and frame-rate figures still stand, but the skew
column does not.

Notes:

- **The A/B skew turned out to be a shared-peripheral transient**, diagnosed
  after this run. Both ADC modules share one `ADC_ETC` block, and each module's
  `startTimer()` read-modify-writes the shared `ADC_ETC.CTRL`. Channel A is
  brought up first, so bringing B up disturbs A's already-live trigger chain and
  A loses a fixed ~150-157 µs of production — which reads as more samples the
  faster the rate (8 at 500 µs/div, 30 at 150, 52 at 100, 157 at 50). Sampling
  the pairing origins inside that window baked the missing samples in
  permanently. Fixed by settling 1 ms before sampling the origins (`d8dbe54`).
  The diagnostic tell was that drift was never positive across 149 reconfigures:
  the loss was always on A, never B.
- The verdict logic now includes skew (tolerance 2 samples). During this run it
  checked tears and overruns only, which is why all nine cells printed `PASS`
  while four violated the skew criterion.
- **The baseline's Rolling 50 µs/div stall is fixed**: 125 ms worst gap against a
  31 ms frame period before, 31 ms now. It was capture-related after all, not
  the draw path.
- **50 µs/div Triggered got slower**: fps 29.5 → 26.1 and worst gap 51 → 71 ms.
  Trigger misses also rose (15508 → 60058, 448/s peak) because the ring lets
  `update()` attempt a search every CAPTURE samples rather than once per
  completed buffer pair. Neither is an acceptance criterion, but the frame-rate
  regression at the shortest timebase is the price paid so far for zero tears.
- 10 ms/div Rolling worst gap rose 159 → 191 ms; every other cell held or
  improved.
- `pairwait` is 0 everywhere now, as expected: nothing waits on a buffer pair.

## Acceptance criteria (from the design spec)

- Zero tears and zero overruns in every cell over the dwell. Checked
  automatically: the `cell:` line ends in `PASS` or `FAIL`.
- Triggered display visually rock-steady at every timebase. Subjective, hence
  the per-cell note.
- A/B skew within ~1 sample. Measured from Phase 2 onward as the distance
  between the two rings' write cursors, reported as `skew=N pk` (worst reading
  in the cell). Not available for the Phase 1 baseline, which had no cursor to
  compare: the old path waited on whole buffers instead.

## Preliminary (pre-instrumentation, Triggered only)

A short unaggregated run before this tooling existed, kept for context only
(`docs/acq-preliminary-002.log`):
33 to 92 s per cell, Triggered only, so not a baseline. Tears were nonzero at
every timebase except 10 ms/div, which already fails criterion 1:

| Timebase | Duration | fps | tears | over |
|----------|----------|-----|-------|------|
| 50 µs/div  | 92 s | 28.2 avg | 608 (6.6/s, 10/s pk) | 0 |
| 500 µs/div | 39 s | 32       | 40 (~1/s)            | 0 |
| 1 ms/div   | 23 s | 32       | 14 (~0.6/s)          | 0 |
| 10 ms/div  | 33 s | 6.3      | 0                    | 0 |

Also observed: `pairwait` counts polls, so it scales with loop speed rather than
acquisition health (0 at 500 µs/div versus ~1.03 M/s at 10 ms/div) and is not
comparable across timebases. `gapmax` tracks 1/fps closely, so it mostly
restates the frame period rather than revealing hitches.
