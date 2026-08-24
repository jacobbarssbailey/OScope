# Do not blit the display with DMA

Measured on hardware, 2026-08-23, Teensy 4.0 + GC9A01A at 48 MHz SPI.

**Conclusion: the display transfer stays synchronous. `updateScreenAsync()`
corrupts the ADC capture stream.** Two attempts at making it asynchronous are
recorded here so the next person does not spend the day rediscovering why.

## Why it looks tempting

A full frame is 240 × 240 × 16 bits = 921,600 bits; at 48 MHz that is **19.2 ms**,
and `ACQ_DIAG` measures 19,596 µs for draw + blit against 490 µs for the draw
alone. The transfer is ~97% of a frame, and `updateScreen()` spends all of it
in a CPU loop — no input polling, no acquisition servicing. Handing it to DMA
appears to be free real estate.

It measures beautifully, too:

|                          | blocking | async  |
| ------------------------ | -------- | ------ |
| display fps              | 50       | 48     |
| draw time (µs)           | 19,596\* | 490    |
| acquisition frames/s     | 51       | 1,362  |
| acquisition service gap  | 20 ms    | 1 ms   |

\* includes the blit, since the loop is stalled inside it.

## Attempt 1: double buffering — leaks a DMA channel per frame

Swapping buffers means calling `setFrameBuffer()` every frame. That clears the
driver's `GC9A01A_DMA_INIT` flag, so the next `updateScreenAsync()` re-runs
`initDMASettings()`, which calls `DMAChannel::begin(true)`. On Teensy 4 that
path skips the "already allocated" early return and **allocates a new channel
without releasing the old one**:

```c
// cores/teensy4/DMAChannel.cpp
void DMAChannel::begin(bool force_initialization) {
    if (!force_initialization && TCD && ...) { return; }   // skipped when forced
    while (1) {                                            // allocates a NEW channel;
        if (!(dma_channel_allocated_mask & (1 << ch))) {   // the old bit stays set
```

`dma_channel_allocated_mask` is a `uint16_t`, so the pool is sixteen channels and
acquisition already holds five:

```
t=2598  loops=1       blits=0  dmaMask=001F     5 channels
t=2698  loops=110953  blits=5  dmaMask=03FF    +5 leaked
t=2798  loops=119509  blits=5  dmaMask=7FFF    +5 leaked
--- USB device dropped ---                      pool empty -> null TCD -> hardfault
```

Eleven frames — about 0.2 s — then the board resets and does it again. From
outside: the display updating every few seconds with the controls dead.

## Attempt 2: single buffer, async transfer — corrupts the ADC stream

No `setFrameBuffer()` per frame, so no leak. Stable: a 75-second soak held
48 fps with no resets. But the tuner read 41 Hz on a signal it read as 230 Hz
with the blocking blit.

Dumping the exact block the pitch detector analyses shows why. Blocking, a clean
ramp:

```
 90:  359  387  411  438  464  490  520  545  570  596
100:  624  648  676  699  729  753  780  805  830  857
```

Asynchronous, the same signal — five samples of ~25 counts each, then a jump of
~80 (three samples' worth missing), repeating:

```
100:  644  609  588  565  538  515  433  412  387  362
110:  336  259  235  212  184  160   82   76  103  125
```

About 30% of samples are gone, ~50 gaps per 512-sample block against **zero**
with the blocking blit. YIN cannot find the real period in that and locks onto
the artefact.

The cause is contention. `updateScreen()` is a **CPU loop** pushing pixels
through the SPI FIFO — it uses no DMA at all, so the two ADC capture channels
have the eDMA engine to themselves. `updateScreenAsync()` adds a third channel
transferring continuously for 19 of every 21 ms. Worse, `DMAChannel::begin()`
sets `DMA_DCHPRI_ECP | DMA_DCHPRI_DPA` on *every* channel it allocates — `DPA`
is "disable preempt ability", so no channel can preempt any other, and the ADC
channels cannot cut in front of the display.

The acquisition subsystem reports it in its own diagnostics: `pairSkew` is
documented as 0-1 samples in practice and measures 0-1 with the blocking blit;
asynchronously it climbs to 20-48.

## If someone wants to try again

The instrument's job is to measure correctly, so nothing here is worth a risk to
sample integrity — a 2 fps gain against silently wrong readings is a bad trade,
and the failure is quiet enough that it took a raw sample dump to see.

The avenue that is left is eDMA arbitration: give the two ADC channels a higher
`DMA_DCHPRI` than the display channel, clear their `DPA` so they *can* preempt,
and set `ECP` on the display channel so it can be preempted. Priorities within a
group must stay unique. Anyone doing this must prove it with the raw-sample test
above — jump count zero over a long soak, `pairSkew` back to 0-1 — and not with
frame-rate numbers, which looked excellent the entire time the samples were being
dropped.
