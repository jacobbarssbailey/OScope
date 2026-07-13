# Acquisition Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make waveform capture rock-solid: instrument the current DMA acquisition to characterize glitches, then replace the double-buffer scheme with per-channel continuous DMA rings the reader trails safely behind.

**Architecture:** Phase 1 (Tasks 1–5) adds unit-testable pure logic (`lib/AcqCore`), an `AcqStats` counter block with tear detection, serial + on-screen diagnostics, and a documented baseline measurement. Phase 2 (Tasks 6–9) introduces `RingCapture` (one eDMA channel streaming ADC results into a 4096-sample DMAMEM ring, write cursor read from the DMA engine) and rewrites `Acquisition::update()` to read only the safe region, then re-runs the same measurement matrix to prove zero tears.

**Tech Stack:** Teensy 4.0 (i.MX RT1062), PlatformIO (`teensy40` env), pedvide ADC library (bundled with the Teensy platform), Teensy core `DMAChannel.h`, PlatformIO native env + Unity for host unit tests.

**Spec:** `docs/superpowers/specs/2026-07-13-acquisition-hardening-design.md`

## Global Constraints

- Branch: `feature/acq-hardening` off `main`.
- Mode-facing behavior unchanged: `Acquisition::update()/frame()/lastTriggered()` keep their signatures; RunScreen and the three modes are not refactored (only additive diag overlay code in RunScreen).
- The CPU must NEVER write to a DMA target buffer (cache-coherency rule from commit `a59ad4f`).
- No dynamic allocation anywhere (project rule — everything statically allocated).
- Ring size 4096 samples per channel, power of two, DMAMEM, aligned to its byte size (8192) for eDMA modulo addressing.
- Guard band: 32 samples (two 32-byte dcache lines) behind the write cursor.
- Diagnostics counters stay in the code permanently; all reporting is gated on `settings.diag` (default off).
- Build: `pio run` (or `just build`). Host tests: `pio test -e native`. Flash: `just run`. Serial monitor: `pio device monitor` (115200).
- Tasks 5 and 9 need the user at the hardware with an external signal generator — they are checkpoints, not autonomous tasks.

---

### Task 1: Native test env + extract trigger search into `lib/AcqCore`

The pure acquisition logic moves to a header-only library so PlatformIO's
native env can unit-test it off-target (PlatformIO ≥ 6 does not build `src/`
for tests by default, so tests link only against `lib/`).

**Files:**
- Create: `lib/AcqCore/AcqCore.h`
- Create: `test/test_acqcore/test_main.cpp`
- Modify: `platformio.ini` (add `[env:native]`)
- Modify: `src/Acquisition.cpp` (use `AcqCore::findTrigger`, delete local copy)

**Interfaces:**
- Produces: `AcqCore::findTrigger(const volatile uint16_t* src, uint16_t searchLen, uint16_t thr, bool rising) -> int` — index of first crossing in `src[1..searchLen]`, or −1. Identical semantics to the current static `findTrigger` in Acquisition.cpp:71.

- [ ] **Step 1: Create branch**

```bash
git checkout main && git checkout -b feature/acq-hardening
```

- [ ] **Step 2: Add the native env to platformio.ini**

Append to `platformio.ini`:

```ini
[env:native]
platform = native
build_flags = -std=c++17
```

- [ ] **Step 3: Write the failing test**

Create `test/test_acqcore/test_main.cpp`:

```cpp
// Host-side unit tests for lib/AcqCore (pure acquisition logic).
#include <unity.h>
#include <AcqCore.h>

void setUp() {}
void tearDown() {}

static void test_rising_edge_found() {
    // 100,200,300 with thr=250 rising: crossing is at index 2 (200<250, 300>=250).
    const uint16_t s[] = {100, 200, 300, 400};
    TEST_ASSERT_EQUAL_INT(2, AcqCore::findTrigger(s, 3, 250, true));
}

static void test_falling_edge_found() {
    const uint16_t s[] = {400, 300, 200, 100};
    TEST_ASSERT_EQUAL_INT(2, AcqCore::findTrigger(s, 3, 250, false));
}

static void test_no_edge_returns_minus_one() {
    const uint16_t s[] = {100, 110, 120, 130};
    TEST_ASSERT_EQUAL_INT(-1, AcqCore::findTrigger(s, 3, 250, true));
}

static void test_exact_threshold_counts_as_crossing() {
    // rising uses >= thr on the right sample.
    const uint16_t s[] = {249, 250};
    TEST_ASSERT_EQUAL_INT(1, AcqCore::findTrigger(s, 1, 250, true));
}

static void test_search_len_zero_finds_nothing() {
    const uint16_t s[] = {0, 1023};
    TEST_ASSERT_EQUAL_INT(-1, AcqCore::findTrigger(s, 0, 512, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rising_edge_found);
    RUN_TEST(test_falling_edge_found);
    RUN_TEST(test_no_edge_returns_minus_one);
    RUN_TEST(test_exact_threshold_counts_as_crossing);
    RUN_TEST(test_search_len_zero_finds_nothing);
    return UNITY_END();
}
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `pio test -e native`
Expected: build FAILS with `AcqCore.h: No such file or directory`.

- [ ] **Step 5: Write the library**

Create `lib/AcqCore/AcqCore.h`:

```cpp
// AcqCore.h — Pure acquisition logic, unit-testable off-target (no Arduino
// dependencies).  Everything here operates on plain sample arrays and 64-bit
// cumulative sample counts; nothing touches hardware.
#pragma once
#include <stdint.h>

namespace AcqCore {

// Scan src[1..searchLen] for the first edge crossing of `thr` in the given
// direction.  Returns the crossing index, or -1 if none.  Pointer is
// const-volatile so raw DMA buffers and plain arrays are both accepted.
inline int findTrigger(const volatile uint16_t* src, uint16_t searchLen,
                       uint16_t thr, bool rising) {
    for (uint16_t t = 1; t <= searchLen; ++t) {
        const bool cross = rising ? (src[t - 1] < thr && src[t] >= thr)
                                  : (src[t - 1] > thr && src[t] <= thr);
        if (cross) return (int)t;
    }
    return -1;
}

}  // namespace AcqCore
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `pio test -e native`
Expected: `5 Tests 0 Failures 0 Ignored` — PASS.

- [ ] **Step 7: Use it from Acquisition.cpp**

In `src/Acquisition.cpp`: add `#include <AcqCore.h>` to the includes, delete
the static `findTrigger` function (lines 69–79), and change the call site
(currently `findTrigger(trigSrc, searchLen, rawThr, rawRising)`) to
`AcqCore::findTrigger(trigSrc, searchLen, rawThr, rawRising)`.

- [ ] **Step 8: Verify the firmware still builds**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 9: Commit**

```bash
git add platformio.ini lib/AcqCore test/test_acqcore src/Acquisition.cpp
git commit -m "test: native unit-test env; extract trigger search into lib/AcqCore"
```

---

### Task 2: AcqStats + tear detection in the current acquisition path

**Files:**
- Create: `src/AcqStats.h`
- Modify: `lib/AcqCore/AcqCore.h` (add `checksum`)
- Modify: `test/test_acqcore/test_main.cpp` (checksum tests)
- Modify: `src/Acquisition.h` (own an `AcqStats`, expose `stats()`)
- Modify: `src/Acquisition.cpp` (collect stats in `update()`)

**Interfaces:**
- Consumes: `AcqCore::findTrigger` (Task 1).
- Produces:
  - `AcqCore::checksum(const volatile uint16_t* src, uint16_t n) -> uint16_t`
  - `struct AcqStats` with public counters `framesProduced, tearEvents, trigMisses, pairWaits, overruns` (all `uint32_t`), `gapMaxUs` (`uint32_t`), and method `noteConsume(uint32_t nowUs)`.
  - `Acquisition::stats() const -> const AcqStats&` and `Acquisition::statsMutable() -> AcqStats&` (the latter used by the 1 Hz reporter in Task 4 to reset window maxima).

- [ ] **Step 1: Write failing checksum tests**

Append to `test/test_acqcore/test_main.cpp` (and register in `main`):

```cpp
static void test_checksum_is_sum_mod_16bit() {
    const uint16_t s[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_UINT16(6, AcqCore::checksum(s, 3));
}

static void test_checksum_detects_single_sample_change() {
    uint16_t s[] = {10, 20, 30, 40};
    const uint16_t before = AcqCore::checksum(s, 4);
    s[2] = 31;
    TEST_ASSERT_NOT_EQUAL(before, AcqCore::checksum(s, 4));
}

static void test_checksum_empty_is_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, AcqCore::checksum(nullptr, 0));
}
```

- [ ] **Step 2: Run tests to verify the new ones fail**

Run: `pio test -e native`
Expected: build FAILS (`checksum` not a member of `AcqCore`).

- [ ] **Step 3: Add checksum to AcqCore.h**

```cpp
// 16-bit additive checksum over n samples.  Used as a tear detector: sum a
// DMA region before and after copying it out; a mismatch means the DMA engine
// overwrote the region mid-read.  Not cryptographic — collisions are
// possible but vanishingly unlikely to hide a real tear (which changes many
// samples).
inline uint16_t checksum(const volatile uint16_t* src, uint16_t n) {
    uint16_t s = 0;
    for (uint16_t i = 0; i < n; ++i) s = (uint16_t)(s + src[i]);
    return s;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: `8 Tests 0 Failures 0 Ignored` — PASS.

- [ ] **Step 5: Create src/AcqStats.h**

```cpp
// AcqStats.h — Acquisition health counters (characterization + regression).
//
// Cumulative event counters plus a per-report-window gap maximum.  Collected
// unconditionally (they are a handful of increments per frame — negligible),
// reported only when settings.diag is on.  Kept permanently so future
// capture regressions are measurable, not mysterious.
#pragma once
#include <stdint.h>

struct AcqStats {
    // Cumulative since boot.
    uint32_t framesProduced = 0;  // update() published a frame
    uint32_t tearEvents     = 0;  // DMA overwrote a buffer while we copied it
    uint32_t trigMisses     = 0;  // Triggered mode: buffer had no edge
    uint32_t pairWaits      = 0;  // polls where exactly one channel was ready
    uint32_t overruns       = 0;  // Phase 2: reader resynced after losing data

    // Max gap between consumed frames (µs) within the current report window;
    // the 1 Hz reporter prints and resets it via resetWindow().
    uint32_t gapMaxUs      = 0;
    uint32_t lastConsumeUs = 0;

    void noteConsume(uint32_t nowUs) {
        if (lastConsumeUs != 0) {
            const uint32_t gap = nowUs - lastConsumeUs;
            if (gap > gapMaxUs) gapMaxUs = gap;
        }
        lastConsumeUs = nowUs;
        ++framesProduced;
    }

    void resetWindow() { gapMaxUs = 0; }
};
```

- [ ] **Step 6: Collect stats in Acquisition**

In `src/Acquisition.h`: add `#include "AcqStats.h"`, a private member
`AcqStats _stats;`, and public accessors:

```cpp
    // Capture-health counters (see AcqStats.h).  statsMutable() exists so the
    // 1 Hz reporter can reset per-window maxima.
    const AcqStats& stats() const { return _stats; }
    AcqStats&       statsMutable() { return _stats; }
```

In `src/Acquisition.cpp`, inside `update()`:

1. Pair-wait counting — replace the early return:

```cpp
    const bool readyA = s_abdmaA.interrupted();
    const bool readyB = s_abdmaB.interrupted();
    if (!readyA || !readyB) {
        if (readyA != readyB) ++_stats.pairWaits;
        return false;
    }
```

2. Trigger-miss counting — in the Triggered branch, right after
   `const int t = findTrigger(...)` (now `AcqCore::findTrigger`):

```cpp
        if (t < 0) ++_stats.trigMisses;
```

3. Tear detection — wrap the copy loop in `if (produce)`. The DMA buffers are
   in cached RAM2 and the CPU cache must be invalidated before the *verify*
   pass, otherwise the re-read sees the stale cached copy and can never
   observe the overwrite. Invalidating whole cache lines inside the DMA
   buffer is safe because the CPU never writes it:

```cpp
    if (produce) {
        uint16_t n = N;
        if (start + n > cap) n = (start < cap) ? (uint16_t)(cap - start) : 0;

        // Tear detector: checksum the source region, copy, invalidate the
        // cache over the region, checksum again.  A mismatch means the DMA
        // engine lapped us and rewrote the region mid-copy.
        const uint16_t sumA = AcqCore::checksum(srcA + start, n);
        const uint16_t sumB = AcqCore::checksum(srcB + start, n);

        SampleBuffers& fb = _buf[_fill];
        for (uint16_t i = 0; i < n; ++i) {
            fb.ch[0][i] = (uint16_t)(kADCMax - srcA[start + i]);
            fb.ch[1][i] = (uint16_t)(kADCMax - srcB[start + i]);
        }
        fb.count = n;

        arm_dcache_delete((void*)(srcA + start), n * sizeof(uint16_t));
        arm_dcache_delete((void*)(srcB + start), n * sizeof(uint16_t));
        if (AcqCore::checksum(srcA + start, n) != sumA ||
            AcqCore::checksum(srcB + start, n) != sumB) {
            ++_stats.tearEvents;
        }

        _stats.noteConsume(micros());
        _lastTriggered = triggered;

        const uint8_t tmp = _show;
        _show = _fill;
        _fill = tmp;
    }
```

(`arm_dcache_delete` is declared in the Teensy core's `imxrt.h`, already
in scope via `<Arduino.h>`.)

- [ ] **Step 7: Verify builds and tests**

Run: `pio run && pio test -e native`
Expected: both SUCCESS/PASS.

- [ ] **Step 8: Commit**

```bash
git add src/AcqStats.h src/Acquisition.h src/Acquisition.cpp lib/AcqCore/AcqCore.h test/test_acqcore/test_main.cpp
git commit -m "feat(acq): capture-health stats with cache-aware tear detection"
```

---

### Task 3: `Settings.diag` toggle (menu row + persistence)

**Files:**
- Modify: `src/Settings.h` (field)
- Modify: `src/Settings.cpp` (defaults, StoredSettings, kVersion bump, adj/fmt, table row)

**Interfaces:**
- Consumes: nothing new.
- Produces: `settings.diag` (`bool`, default `false`), menu row "Diag" (On/Off). MenuScreen and EditValueScreen pick the row up automatically from `settingItems()`/`settingCount()`.

Note: `StoredSettings` grows from 7 to 8 bytes; ScopeState's EEPROM record
starts at address 32 (`ScopeState.cpp:9`), so there is no collision. Bumping
`kVersion` to 2 makes existing EEPROM records fall back to defaults once
(expected, harmless).

- [ ] **Step 1: Add the field**

In `src/Settings.h`, inside `struct Settings` after `grid`:

```cpp
    bool       diag       = false;   // capture diagnostics (serial + overlay)
```

- [ ] **Step 2: Wire persistence and the menu row**

In `src/Settings.cpp`:

- `defaults()`: add `diag = false;`
- `kVersion`: change `1` → `2`.
- `StoredSettings`: add `bool diag;` after `grid`.
- `load()`: add `diag = s.diag;` in the valid-record branch.
- `save()`: add `diag` to the initializer: `StoredSettings s{kMagic, kVersion, trigSource, trigEdge, trigMode, grid, diag};`
- Add adjust/format helpers after `adjGrid`/`fmtGrid`:

```cpp
static void adjDiag(Settings& s, int8_t d) {
    if (d) s.diag = !s.diag;
}
static void fmtDiag(const Settings& s, char* b, uint8_t n) {
    snprintf(b, n, "%s", s.diag ? "On" : "Off");
}
```

- Add to `kItems`: `{ "Diag", adjDiag, fmtDiag },`

- [ ] **Step 3: Build**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/Settings.h src/Settings.cpp
git commit -m "feat(settings): Diag toggle for capture diagnostics (EEPROM v2)"
```

---

### Task 4: 1 Hz serial report + on-screen diag overlay

**Files:**
- Modify: `src/Acquisition.h` / `src/Acquisition.cpp` (reporter)
- Modify: `src/Theme.h` (overlay Y position)
- Modify: `src/screens/RunScreen.cpp` (overlay draw)

**Interfaces:**
- Consumes: `AcqStats` (Task 2), `settings.diag` (Task 3).
- Produces: serial line at ~1 Hz when diag is on, format
  `acq: fps=NN exp=NN frames=NN tears=NN miss=NN pairwait=NN over=NN gapmax=NNms`
  (per-second deltas for counters, cumulative totals in parentheses for
  tears; `exp` = the buffer completion rate implied by the current timebase,
  so fps vs exp is the spec's "missed buffers" metric); a 1 Hz `draw: max=NNms`
  line from OScope.ino (render+blit time); `Theme::DiagY` (`int16_t`, value 50).

- [ ] **Step 1: Add the reporter to Acquisition**

`src/Acquisition.h` — private members and a public hook:

```cpp
    // 1 Hz diagnostics reporter state (deltas since the previous report).
    uint32_t _repLastMs      = 0;
    uint32_t _repLastFrames  = 0;
    uint32_t _repLastTears   = 0;
    uint32_t _repLastMisses  = 0;
    uint32_t _repLastWaits   = 0;
    uint32_t _repLastOver    = 0;
```

```cpp
    // Print a 1 Hz stats line to Serial while diagnostics are enabled.
    // Call once per loop; cheap no-op between reports.
    void reportDiag(bool enabled);
```

`src/Acquisition.cpp`:

```cpp
void Acquisition::reportDiag(bool enabled) {
    if (!enabled) return;
    const uint32_t now = millis();
    if (_repLastMs != 0 && now - _repLastMs < 1000) return;
    if (_repLastMs == 0) { _repLastMs = now; return; }   // first call: arm only

    // Expected buffer completion rate at the current timebase: one buffer per
    // CAPTURE samples.  fps well below exp = the reader is missing buffers.
    const uint32_t interval = ((uint32_t)_sTimebase * Theme::GridCols)
                              / SampleBuffers::N;
    const uint32_t exp = (1000000UL / (interval < 1 ? 1 : interval)) / CAPTURE;

    Serial.printf("acq: fps=%lu exp=%lu frames=%lu tears=%lu(%lu) miss=%lu pairwait=%lu over=%lu gapmax=%lums\n",
        (unsigned long)(_stats.framesProduced - _repLastFrames),
        (unsigned long)exp,
        (unsigned long)_stats.framesProduced,
        (unsigned long)(_stats.tearEvents - _repLastTears),
        (unsigned long)_stats.tearEvents,
        (unsigned long)(_stats.trigMisses - _repLastMisses),
        (unsigned long)(_stats.pairWaits - _repLastWaits),
        (unsigned long)(_stats.overruns - _repLastOver),
        (unsigned long)(_stats.gapMaxUs / 1000));

    _repLastMs     = now;
    _repLastFrames = _stats.framesProduced;
    _repLastTears  = _stats.tearEvents;
    _repLastMisses = _stats.trigMisses;
    _repLastWaits  = _stats.pairWaits;
    _repLastOver   = _stats.overruns;
    _stats.resetWindow();
}
```

Call it from `RunScreen::tick()` right after the `_acq.update(...)` line:

```cpp
    _acq.reportDiag(ctx.settings.diag);
```

- [ ] **Step 2: Add the overlay position to Theme.h**

After `StopY`:

```cpp
  constexpr int16_t DiagY   = 50;   // capture-diagnostics overlay (Arial 13)
```

- [ ] **Step 3: Draw the overlay in RunScreen::draw()**

After the selected-parameter readout (step 4 of the draw sequence), before the
mode flash:

```cpp
    // Capture diagnostics overlay: cumulative tears, frames/s consumed, and
    // the worst inter-frame gap in the last report window.
    if (ctx.settings.diag) {
        const AcqStats& st = _acq.stats();
        char d[40];
        snprintf(d, sizeof d, "T%lu F%lu G%lums",
                 (unsigned long)st.tearEvents,
                 (unsigned long)st.framesProduced,
                 (unsigned long)(st.gapMaxUs / 1000));
        r.textCenterX(Theme::DiagY, d, Theme::Dim, Arial_13);
    }
```

(`AcqStats.h` is already included via RunScreen.h → Acquisition.h chain; add
`#include "../AcqStats.h"` to RunScreen.cpp only if the build complains.)

- [ ] **Step 4: Measure render+blit time in OScope.ino**

The loop's draw-and-blit block is the suspected reason the reader trails DMA,
so its duration is a first-class stat. In `OScope.ino`, wrap the redraw block
in `loop()` and report a 1 Hz maximum when diag is on:

```cpp
    if (uiDirty || newFrame) {
        const uint32_t drawStart = micros();
        screens.draw(renderer, ctx);
        // ... existing FPS overlay code unchanged ...
        tft.updateScreen();

        // Diag: track the slowest draw+blit each second — this is the time
        // the loop cannot consume DMA buffers.
        static uint32_t drawMaxUs = 0, drawRepMs = 0;
        const uint32_t drawUs = micros() - drawStart;
        if (drawUs > drawMaxUs) drawMaxUs = drawUs;
        if (settings.diag && millis() - drawRepMs >= 1000) {
            Serial.printf("draw: max=%lums\n", (unsigned long)(drawMaxUs / 1000));
            drawMaxUs = 0;
            drawRepMs = millis();
        }

        countFrame();
        uiDirty = false;
    }
```

- [ ] **Step 5: Build and flash-check**

Run: `pio run`
Expected: SUCCESS. If hardware is attached: `just run`, enable Settings → Diag,
confirm the overlay appears and `pio device monitor` shows the 1 Hz lines.

- [ ] **Step 6: Commit**

```bash
git add src/Acquisition.h src/Acquisition.cpp src/Theme.h src/screens/RunScreen.cpp src/OScope.ino
git commit -m "feat(acq): 1Hz serial diagnostics report + on-screen overlay"
```

---

### Task 5: Characterization protocol doc + Phase 1 baseline (USER AT HARDWARE)

**Files:**
- Create: `docs/acq-characterization.md`

**Interfaces:** none (documentation + measurement).

This task is a checkpoint: the numbers must be gathered by the user with the
scope on the bench and an external generator producing a 1 kHz triangle
(≈±5 V works well at 3 V/div).

- [ ] **Step 1: Write the protocol doc**

Create `docs/acq-characterization.md`:

```markdown
# Acquisition Characterization Protocol

Feed a 1 kHz triangle (~±5 V) into channel A (and B where noted). Enable
Settings → Diag. For each cell: set the timebase and mode, watch the display,
and record the serial stats after ~5 minutes (the 1 Hz line prints per-second
deltas and cumulative totals).

Record: tears/s (and cumulative), frames/s, gapmax, trig miss/s, pairwaits/s,
plus a subjective note (steady / occasional jump / frequent glitches).

## Baseline (before ring capture) — commit <hash>, date <date>

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
```

- [ ] **Step 2: Flash and hand off to the user**

Run: `just run`, then ask the user to work through the baseline matrix and
paste the serial lines back. Fill in the baseline table with real numbers and
the commit hash.

- [ ] **Step 3: Commit**

```bash
git add docs/acq-characterization.md
git commit -m "docs(acq): characterization protocol + Phase 1 baseline numbers"
```

---

### Task 6: Ring cursor math in AcqCore (pure, TDD)

**Files:**
- Modify: `lib/AcqCore/AcqCore.h`
- Modify: `test/test_acqcore/test_main.cpp`

**Interfaces:**
- Produces (all in `namespace AcqCore`, all `inline`):
  - `ringIndex(uint64_t count, uint32_t ringSize) -> uint32_t` — position of a cumulative count inside a power-of-two ring.
  - `safeWatermark(uint64_t written, uint32_t guard) -> uint64_t` — newest cumulative count the reader may touch.
  - `newestWindow(uint64_t safe, uint32_t len, uint64_t* base) -> bool` — base of the newest `len`-sample window ending at `safe`; false if not enough data yet.

- [ ] **Step 1: Write failing tests**

Append to `test/test_acqcore/test_main.cpp` (register in `main`):

```cpp
static void test_ring_index_wraps_power_of_two() {
    TEST_ASSERT_EQUAL_UINT32(0,    AcqCore::ringIndex(0, 4096));
    TEST_ASSERT_EQUAL_UINT32(4095, AcqCore::ringIndex(4095, 4096));
    TEST_ASSERT_EQUAL_UINT32(0,    AcqCore::ringIndex(4096, 4096));
    TEST_ASSERT_EQUAL_UINT32(5,    AcqCore::ringIndex(4096ULL * 1000 + 5, 4096));
}

static void test_safe_watermark_trails_by_guard() {
    TEST_ASSERT_EQUAL_UINT64(968, AcqCore::safeWatermark(1000, 32));
}

static void test_safe_watermark_clamps_to_zero() {
    TEST_ASSERT_EQUAL_UINT64(0, AcqCore::safeWatermark(10, 32));
}

static void test_newest_window_when_enough_data() {
    uint64_t base = 0;
    TEST_ASSERT_TRUE(AcqCore::newestWindow(1000, 480, &base));
    TEST_ASSERT_EQUAL_UINT64(520, base);
}

static void test_newest_window_insufficient_data() {
    uint64_t base = 99;
    TEST_ASSERT_FALSE(AcqCore::newestWindow(479, 480, &base));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: build FAILS (`ringIndex` not a member).

- [ ] **Step 3: Implement in AcqCore.h**

```cpp
// ---- Ring cursor protocol --------------------------------------------------
// Cumulative 64-bit sample counts never wrap in practice (2^64 samples at
// 1 Msps ≈ 585,000 years); all reader/writer positions are counts, converted
// to ring offsets only at the memory access.

// Position of cumulative count within a power-of-two ring.
inline uint32_t ringIndex(uint64_t count, uint32_t ringSize) {
    return (uint32_t)(count & (uint64_t)(ringSize - 1));
}

// Newest cumulative count the reader may touch: the DMA write head minus a
// guard band (whole dcache lines that may still be landing).
inline uint64_t safeWatermark(uint64_t written, uint32_t guard) {
    return written > guard ? written - guard : 0;
}

// Base of the newest len-sample window ending at `safe`.  False until enough
// samples exist.
inline bool newestWindow(uint64_t safe, uint32_t len, uint64_t* base) {
    if (safe < len) return false;
    *base = safe - len;
    return true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: `13 Tests 0 Failures 0 Ignored` — PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/AcqCore/AcqCore.h test/test_acqcore/test_main.cpp
git commit -m "feat(acqcore): ring cursor math (index, safe watermark, newest window)"
```

---

### Task 7: RingCapture — continuous eDMA ring for one ADC channel

**Files:**
- Create: `src/RingCapture.h`
- Create: `src/RingCapture.cpp`

**Interfaces:**
- Consumes: `AcqCore::ringIndex`.
- Produces:

```cpp
class RingCapture {
public:
    static constexpr uint32_t Size = 4096;   // samples; power of two

    // One-time setup: point the eDMA channel at this ADC's result register,
    // circular-destination into `ring` (must be DMAMEM, aligned(8192)),
    // enable ADC DMA requests.  adcNum is 0 (ADC1 hw) or 1 (ADC2 hw), matching
    // the pedvide library's module numbering used elsewhere in this codebase.
    void begin(ADC* adc, uint8_t adcNum, uint8_t pin, volatile uint16_t* ring);

    void start(uint32_t freqHz);   // (re)start timer-paced conversions
    void stop();

    uint64_t totalWritten();       // cumulative samples DMA has written

    // Copy n samples starting at cumulative count `from` into dst, handling
    // ring wrap and dcache invalidation.  Caller must keep [from, from+n)
    // within the safe region (behind safeWatermark(totalWritten(), guard)).
    void read(uint64_t from, uint16_t* dst, uint16_t n);
};
```

**Prerequisite reading for the implementer** (exact register/macro choices
must be mirrored from the working library, not invented):

- `~/.platformio/packages/framework-arduinoteensy/libraries/ADC/AnalogBufferDMA.cpp` — the `__IMXRT1062__` sections show the correct DMAMUX source (`DMAMUX_SOURCE_ADC1` / `DMAMUX_SOURCE_ADC2`), the ADC-side DMA enable, and the dcache maintenance calls this class replaces.
- `~/.platformio/packages/framework-arduinoteensy/libraries/ADC/ADC_Module.cpp` — `startTimer()` / QuadTimer path (we keep using it via the pedvide API, exactly as `Acquisition::configureTimer` does today).
- Teensy core `DMAChannel.h` — `destinationCircular()`, `triggerAtHardwareEvent()`, `interruptAtCompletion()`, `attachInterrupt()`, and the `TCD->DADDR` field used for the write cursor.

- [ ] **Step 1: Write RingCapture.h**

```cpp
// RingCapture.h — Continuous eDMA capture of one ADC channel into a ring.
//
// The DMA engine streams every timer-paced ADC conversion into a 4096-sample
// DMAMEM ring using hardware modulo addressing; it never stops and is never
// waited on.  Readers trail the hardware write cursor (TCD DADDR) behind a
// guard band, which makes torn reads structurally impossible — see
// docs/superpowers/specs/2026-07-13-acquisition-hardening-design.md.
//
// Write cursor: DADDR gives the position within the ring; a major-loop
// completion interrupt counts wraps.  totalWritten() combines the two into a
// 64-bit cumulative count, re-reading until stable so a wrap between the two
// reads cannot produce a torn value.
//
// The CPU never writes the ring (cache-coherency rule, commit a59ad4f);
// read() invalidates the dcache over exactly the region it copies out.
#pragma once
#include <stdint.h>
#include <DMAChannel.h>

class ADC;

class RingCapture {
public:
    static constexpr uint32_t Size = 4096;   // samples; power of two (8 KB)

    void begin(ADC* adc, uint8_t adcNum, uint8_t pin, volatile uint16_t* ring);
    void start(uint32_t freqHz);
    void stop();

    uint64_t totalWritten();
    void read(uint64_t from, uint16_t* dst, uint16_t n);

private:
    DMAChannel         _dma;
    ADC*               _adc     = nullptr;
    uint8_t            _adcNum  = 0;
    uint8_t            _pin     = 0;
    volatile uint16_t* _ring    = nullptr;
    volatile uint32_t  _wraps   = 0;   // major-loop completions (ISR-owned)

    static RingCapture* s_instance[2];   // ISR trampolines, one per ADC
    static void isr0();
    static void isr1();
    void onWrap() { ++_wraps; _dma.clearInterrupt(); }
};
```

- [ ] **Step 2: Write RingCapture.cpp**

The skeleton below is complete except where marked `// MIRROR:`, which must be
copied from the library files listed above (do that reading first — the
DMAMUX source macro name and the exact ADC DMA-enable call are the two things
to confirm, not design):

```cpp
#include "RingCapture.h"
#include <ADC.h>
#include <Arduino.h>
#include <AcqCore.h>

RingCapture* RingCapture::s_instance[2] = {nullptr, nullptr};

void RingCapture::isr0() { if (s_instance[0]) s_instance[0]->onWrap(); }
void RingCapture::isr1() { if (s_instance[1]) s_instance[1]->onWrap(); }

void RingCapture::begin(ADC* adc, uint8_t adcNum, uint8_t pin,
                        volatile uint16_t* ring) {
    _adc = adc; _adcNum = adcNum; _pin = pin; _ring = ring;
    s_instance[adcNum] = this;

    ADC_Module* m = (adcNum == 0) ? adc->adc0 : adc->adc1;
    m->enableDMA();   // MIRROR: confirm against AnalogBufferDMA.cpp T4 path

    _dma.begin();
    // MIRROR: source register + DMAMUX source for this ADC module, per
    // AnalogBufferDMA.cpp (__IMXRT1062__ sections).
    _dma.source(/* ADC result register for this module */);
    _dma.triggerAtHardwareEvent(/* DMAMUX_SOURCE_ADC1 or _ADC2 */);
    _dma.destinationCircular(_ring, Size * sizeof(uint16_t));
    _dma.interruptAtCompletion();
    _dma.attachInterrupt(adcNum == 0 ? isr0 : isr1);
    _dma.enable();
}

void RingCapture::start(uint32_t freqHz) {
    ADC_Module* m = (_adcNum == 0) ? _adc->adc0 : _adc->adc1;
    m->stopTimer();
    m->startSingleRead(_pin);
    m->startTimer(freqHz);
}

void RingCapture::stop() {
    ADC_Module* m = (_adcNum == 0) ? _adc->adc0 : _adc->adc1;
    m->stopTimer();
}

uint64_t RingCapture::totalWritten() {
    // DADDR advances sample by sample; wraps counts major-loop completions.
    // Read until stable so a wrap between the two loads can't tear the value.
    uint32_t wraps1, offset;
    do {
        wraps1  = _wraps;
        offset  = ((uint32_t)_dma.TCD->DADDR - (uint32_t)(uintptr_t)_ring)
                  / sizeof(uint16_t);
    } while (wraps1 != _wraps);
    return (uint64_t)wraps1 * Size + offset;
}

void RingCapture::read(uint64_t from, uint16_t* dst, uint16_t n) {
    uint32_t idx = AcqCore::ringIndex(from, Size);
    uint16_t first = (uint16_t)((idx + n <= Size) ? n : (Size - idx));

    arm_dcache_delete((void*)(_ring + idx), first * sizeof(uint16_t));
    memcpy(dst, (const void*)(_ring + idx), first * sizeof(uint16_t));

    if (first < n) {   // wrapped: remainder from the ring start
        const uint16_t rest = (uint16_t)(n - first);
        arm_dcache_delete((void*)_ring, rest * sizeof(uint16_t));
        memcpy(dst + first, (const void*)_ring, rest * sizeof(uint16_t));
    }
}
```

Note on `destinationCircular`: it requires the destination aligned to its byte
size. The rings are declared in Acquisition.cpp (Task 8) as:

```cpp
DMAMEM static volatile uint16_t __attribute__((aligned(8192))) s_ringA[RingCapture::Size];
DMAMEM static volatile uint16_t __attribute__((aligned(8192))) s_ringB[RingCapture::Size];
```

- [ ] **Step 3: Build**

Run: `pio run`
Expected: SUCCESS (class compiles; not yet wired in).

- [ ] **Step 4: Commit**

```bash
git add src/RingCapture.h src/RingCapture.cpp
git commit -m "feat(acq): RingCapture — continuous eDMA ring with hardware write cursor"
```

---

### Task 8: Rewrite Acquisition on dual RingCapture (drop AnalogBufferDMA)

**Files:**
- Modify: `src/Acquisition.h`
- Modify: `src/Acquisition.cpp`

**Interfaces:**
- Consumes: `RingCapture` (Task 7), `AcqCore` helpers (Tasks 1, 2, 6), `AcqStats` (Task 2).
- Produces: unchanged public API — `begin()`, `update(state, settings) -> bool`, `frame() -> const SampleBuffers&`, `lastTriggered() -> bool`, `stats()`, `reportDiag(bool)`. New public members for follow-up branches: `capA() -> RingCapture&`, `capB() -> RingCapture&` (rolling/X-Y branches will consume these; unused on this branch).

- [ ] **Step 1: Replace the DMA plumbing**

In `src/Acquisition.cpp`:

- Delete the four `s_dmaBuf*` arrays, both `AnalogBufferDMA` objects, and the `<AnalogBufferDMA.h>` include.
- Add:

```cpp
#include "RingCapture.h"

// Continuous capture rings, one per channel (A on ADC0/SIGNAL_A, B on
// ADC1/SIGNAL_B).  aligned(8192) is required by eDMA modulo addressing.
DMAMEM static volatile uint16_t __attribute__((aligned(8192))) s_ringA[RingCapture::Size];
DMAMEM static volatile uint16_t __attribute__((aligned(8192))) s_ringB[RingCapture::Size];

static RingCapture s_capA;
static RingCapture s_capB;

// Reader must trail the DMA head by whole dcache lines still being filled.
static constexpr uint32_t kGuard = 32;   // samples = two 32-byte lines
```

- `begin()`: keep the ADC configuration block (averaging/resolution/speeds)
  verbatim; replace the two `s_abdma*.init(...)` calls with:

```cpp
    s_capA.begin(&s_adc, 0, SIGNAL_A, s_ringA);
    s_capB.begin(&s_adc, 1, SIGNAL_B, s_ringB);
```

- `configureTimer()`: replace the stop/startSingleRead/startTimer sequence with:

```cpp
    s_capA.start(freq);
    s_capB.start(freq);
    _nextProduceAt = 0;   // produce as soon as the rings refill
```

- [ ] **Step 2: Rewrite update() around the safe-region read**

Replace the body after the timer-reconfigure block with:

```cpp
    // Both rings run at the same rate; pair by index at the lower watermark.
    const uint64_t wA = s_capA.totalWritten();
    const uint64_t wB = s_capB.totalWritten();
    _stats.pairSkew = (uint32_t)((wA > wB) ? (wA - wB) : (wB - wA));

    const uint64_t safe = AcqCore::safeWatermark((wA < wB) ? wA : wB, kGuard);

    // Produce at most one frame per CAPTURE new samples — same cadence as the
    // old double-buffer scheme, so frame rates match the Phase 1 baseline.
    if (safe < _nextProduceAt || safe < CAPTURE) return false;

    // Read the newest CAPTURE-sample region (raw, still hardware-inverted)
    // into linear scratch; all search/copy logic below is linear and simple.
    uint64_t base = 0;
    AcqCore::newestWindow(safe, CAPTURE, &base);
    static uint16_t scratchA[CAPTURE];
    static uint16_t scratchB[CAPTURE];
    s_capA.read(base, scratchA, CAPTURE);
    s_capB.read(base, scratchB, CAPTURE);
    _nextProduceAt = safe + CAPTURE;

    uint16_t start = 0;
    bool     triggered = true;
    bool     produce   = true;

    if (state.mode == Mode::Triggered) {
        const uint16_t searchLen = N;   // leaves a full N window after any hit
        const uint16_t* trigSrc  = (settings.trigSource == TrigSource::B)
                                   ? scratchB : scratchA;
        const uint16_t rawThr    = (uint16_t)(kADCMax - triggerADC(state.trigger_level_mv));
        const bool     rawRising = (settings.trigEdge != TrigEdge::Rising);

        const int t = AcqCore::findTrigger(trigSrc, searchLen, rawThr, rawRising);
        if (t >= 0) {
            start = (uint16_t)t;
            triggered = true;
            _autoMissCount = 0;
        } else {
            ++_stats.trigMisses;
            if (settings.trigMode == TrigMode::Auto) {
                if (_autoMissCount < kAutoFreerunMisses) {
                    ++_autoMissCount;
                    produce = false;
                } else {
                    start = 0;
                    triggered = false;
                }
            } else {
                produce = false;   // Normal: hold last frame
            }
        }
    }

    if (produce) {
        SampleBuffers& fb = _buf[_fill];
        for (uint16_t i = 0; i < N; ++i) {
            fb.ch[0][i] = (uint16_t)(kADCMax - scratchA[start + i]);
            fb.ch[1][i] = (uint16_t)(kADCMax - scratchB[start + i]);
        }
        fb.count = N;

        // Tear verification (diag only): the safe-region protocol makes tears
        // structurally impossible; re-read the same region and prove it.
        if (_diagVerify) {
            static uint16_t verifyBuf[CAPTURE];
            s_capA.read(base, verifyBuf, CAPTURE);
            if (AcqCore::checksum(verifyBuf, CAPTURE) !=
                AcqCore::checksum(scratchA, CAPTURE)) ++_stats.tearEvents;
        }

        _stats.noteConsume(micros());
        _lastTriggered = triggered;

        const uint8_t tmp = _show;
        _show = _fill;
        _fill = tmp;
    }

    return produce;
```

Supporting changes:

- `src/Acquisition.h`: remove `_autoMissCount`'s neighbors that no longer
  exist; add privates `uint64_t _nextProduceAt = 0; bool _diagVerify = false;`
  and publics `RingCapture& capA(); RingCapture& capB();` (defined in the
  .cpp returning `s_capA`/`s_capB`).
- `AcqStats`: add `uint32_t pairSkew = 0;` (instantaneous, samples) and print
  it in `reportDiag` as ` skew=NN`.
- Set `_diagVerify` from `update()`'s settings argument: `_diagVerify = settings.diag;`
- `sampleIntervalUs`/`triggerADC` and the Auto-hold logic are unchanged.
- Delete the old pair-wait counting (`pairWaits` stays in AcqStats, now
  reported as 0 — it documents the old failure mode in baseline data).
- Overrun accounting note: the spec's overrun-resync applies to a *contiguous*
  consumer that must not lose data. The display path deliberately reads only
  the newest window (skipping is normal, not an error), so no overrun can
  occur here; the `overruns` counter stays in AcqStats and is exercised by
  the rolling-mode follow-up branch, which consumes the ring contiguously via
  `RingCapture::read` + its own cursor.

- [ ] **Step 3: Build and run host tests**

Run: `pio run && pio test -e native`
Expected: both PASS. (`RingCapture` is hardware code, exercised in Task 9;
the cursor/search math it depends on is covered by the native tests.)

- [ ] **Step 4: Bring-up on hardware (channel A first)**

Flash (`just run`) with the generator on channel A. Sanity sequence:

1. Diag on, Triggered mode, 500 µs/div: confirm `frames/s` is nonzero and the
   trace is a stable triangle.
2. If channel A is dead, temporarily comment out `s_capB.begin/start` calls to
   isolate — a mis-mirrored DMAMUX source shows up as `frames=0` (watermark
   never advances) or garbage samples.
3. Confirm `skew=` stays at 0–1 samples.

- [ ] **Step 5: Commit**

```bash
git add src/Acquisition.h src/Acquisition.cpp src/AcqStats.h
git commit -m "feat(acq): ring-based acquisition — safe-region reads replace AnalogBufferDMA"
```

---

### Task 9: Phase 2 characterization re-run (USER AT HARDWARE)

**Files:**
- Modify: `docs/acq-characterization.md` (fill the "After" table)

- [ ] **Step 1: Re-run the matrix**

Same protocol as Task 5, same generator settings. Fill the "After ring
capture" table with the new commit hash and numbers.

- [ ] **Step 2: Check acceptance criteria**

- tears = 0 and over = 0 in every cell over 5 minutes,
- Triggered display visually rock-steady at every timebase,
- skew ≤ 1 sample.

If any cell fails, debug with the superpowers:systematic-debugging skill
before proceeding — do not average away failures.

- [ ] **Step 3: Commit**

```bash
git add docs/acq-characterization.md
git commit -m "docs(acq): Phase 2 characterization — ring capture results"
```

---

### Task 10: Documentation sweep + finish the branch

**Files:**
- Modify: `src/Acquisition.h`, `src/Acquisition.cpp` (header comments describe the ring design, not AnalogBufferDMA)
- Modify: `README.md` (only if it describes the acquisition internals)

- [ ] **Step 1: Update stale comments**

Rewrite the file-header comments of `Acquisition.h`/`Acquisition.cpp` to
describe: continuous per-channel eDMA rings, hardware write cursor + guard
band, linear scratch windows, and where the incremental-read API for future
rolling/X-Y work lives (`capA()`/`capB()`, `RingCapture::read`). Check
`grep -rn "AnalogBufferDMA" src README.md` returns nothing.

- [ ] **Step 2: Final verification**

Run: `pio run && pio test -e native`
Expected: both PASS. Then use the superpowers:verification-before-completion
skill, followed by superpowers:finishing-a-development-branch (merge vs PR is
the user's call).

- [ ] **Step 3: Commit**

```bash
git add -A src README.md
git commit -m "docs(acq): describe ring-capture architecture in file headers"
```
