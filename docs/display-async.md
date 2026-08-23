# Display transfer: why the blit is asynchronous but not double buffered

Measured on hardware, 2026-08-23, Teensy 4.0 + GC9A01A at 48 MHz SPI.

## The transfer is the frame budget

A full frame is 240 × 240 × 16 bits = 921,600 bits. At 48 MHz that is **19.2 ms**,
and `ACQ_DIAG` measures 19,596 µs for draw + blit against 490 µs for the draw
alone — so the transfer is ~97% of a frame and drawing is noise beside it. (The
same arithmetic reproduces the "~30 ms at 30 MHz" figure recorded in
`OScope.ino`, which is where the 48 MHz clock came from.)

With the original blocking `updateScreen()` the CPU sat inside that 19.2 ms
doing nothing: no input polling, no acquisition servicing.

## What asynchronous buys

`DISPLAY_ASYNC_BLIT` hands the frame to DMA and lets the loop carry on. The next
draw is gated on `asyncUpdateActive()`, because there is one framebuffer and
drawing into it mid-transfer would tear.

|                          | blocking | async  |
| ------------------------ | -------- | ------ |
| display fps              | 50       | 48     |
| draw time (µs)           | 19,596\* | 490    |
| acquisition frames/s     | 51       | 1,362  |
| acquisition service gap  | 20 ms    | 1 ms   |

\* includes the blit, since the loop is stalled inside it.

Display frame rate goes *down* slightly — the loop now checks whether the panel
is free only once per iteration, and an iteration does a full acquisition tick.
That is a good trade: the 20 ms → 1 ms service gap is the real result. At
70 µs/div the sample interval is ~2.3 µs, so a 4096-sample ring fills in ~9.5 ms
— a 20 ms gap meant the writer lapped the reader twice between reads. Input
latency improves for the same reason: buttons are polled tens of thousands of
times a second instead of fifty.

## Why not double buffering

Double buffering would let the draw overlap the transfer, making a frame cost
`max(draw, blit)` rather than `draw + blit`. Given draw = 490 µs against a
19.2 ms blit, that is worth about 4%.

It was tried (commit reverted in this branch) and it hardfaults, for a reason
worth writing down: swapping buffers means calling `setFrameBuffer()` every
frame. That clears the driver's `GC9A01A_DMA_INIT` flag, so the next
`updateScreenAsync()` re-runs `initDMASettings()`, which calls
`DMAChannel::begin(true)`. On Teensy 4 that path skips the "already allocated"
early return and **allocates a new channel without releasing the old one**:

```c
// cores/teensy4/DMAChannel.cpp
void DMAChannel::begin(bool force_initialization) {
    if (!force_initialization && TCD && ...) { return; }   // skipped when forced
    while (1) {                                            // allocates a NEW channel;
        if (!(dma_channel_allocated_mask & (1 << ch))) {   // the old bit stays set
```

`dma_channel_allocated_mask` is a `uint16_t`, so the pool is 16 channels and
acquisition already holds five. Instrumenting the loop showed it exactly:

```
t=2598  loops=1       blits=0  dmaMask=001F     5 channels
t=2698  loops=110953  blits=5  dmaMask=03FF    +5 leaked
t=2798  loops=119509  blits=5  dmaMask=7FFF    +5 leaked
--- USB device dropped ---                      pool empty -> null TCD -> hardfault
```

Eleven frames — about 0.2 s — then `begin()` returns `TCD = 0` and the next use
faults. The board resets, draws another eleven frames, faults again: from the
outside it looks like the display updating every few seconds and the controls
being dead.

The asynchronous machinery itself is fine — `blits=5` per 100 ms is 50 fps, and
`busy=109039` shows the loop spinning happily through the transfer. Only the
per-frame `setFrameBuffer()` is fatal.

Reviving double buffering therefore needs the leak fixed, which means patching
the library or the core rather than anything in this project. For 4%, it is not
worth carrying a fork. A 75-second soak of the single-buffer async path holds
48 fps with no resets, `tears=0` and `over=0`.
