# OScope Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evolve the OScope test sketch into a structured, dual-channel scope firmware with the approved control scheme, built so UI controls, display style, modes, and settings are each easy to change.

**Architecture:** Layered with small interfaces per axis-of-change — an **input-event** layer (physical → events), a **screen stack** (navigation), a **Parameter** abstraction (unifies the encoder cycle and settings), a **ScopeMode** strategy (one class per mode), a central **ScopeState**, and a **Theme/Renderer** for display style. See the design doc: `docs/superpowers/specs/2026-06-22-oscope-ui-design.md`.

**Tech Stack:** Teensy 4.0, Arduino framework, PlatformIO. Libraries: Adafruit GFX, GC9A01A_t3n (framebuffered display), Encoder (quadrature). Build/flash via `just build` / `just run` / `just debug`.

## Global Constraints

- Target board: Teensy 4.0 (`platformio.ini` env `teensy40`); build must keep succeeding (`just build` → `[SUCCESS]`).
- Display is framebuffered: draw into `fb1`, then `tft.updateScreen()` once per frame. Never draw directly to the panel.
- Round 240×240 display: keep all readable content within a central safe band (inset ≈ 30 px from edges).
- Buttons are active-low, `INPUT_PULLUP`; long-press threshold = **500 ms**; short press fires on release, long press fires once at threshold.
- No dynamic allocation in the hot path; prefer static buffers and stack/global objects (embedded).
- Pin assignments come from `Config.h` only (single source of truth); they must match `README.md`.
- Each milestone must compile, flash, and pass an on-hardware observation before the next begins.

---

## File Structure

All firmware lives in `src/` (PlatformIO compiles every `.cpp`/`.h` there).

| File | Responsibility |
|------|----------------|
| `src/OScope.ino` | `setup()`/`loop()` only: construct objects, wire them, run the frame loop. Thin. |
| `src/Config.h` | Pin defines, screen size, timing constants, default values. |
| `src/Theme.h` | Colors, layout constants, font sizes. Display style lives here. |
| `src/Input.h` / `.cpp` | Debounced buttons + encoder → `InputEvent`s. |
| `src/Renderer.h` / `.cpp` | Thin helper layer over `GC9A01A_t3n` using `Theme`. |
| `src/ScopeState.h` / `.cpp` | `ScopeState` struct, enums, `resetToDefaults()`. |
| `src/Parameter.h` / `.cpp` | `Parameter` descriptors + the per-mode encoder cycle. |
| `src/Acquisition.h` / `.cpp` | ADC sampling of both channels into `SampleBuffers`. |
| `src/modes/ScopeMode.h` | `ScopeMode` interface + `SampleBuffers`. |
| `src/modes/TriggeredMode.{h,cpp}` `RollingMode.{h,cpp}` `XYMode.{h,cpp}` | One class per mode. |
| `src/screens/Screen.h` | `Screen` interface, `ScreenStack`, `AppContext`. |
| `src/screens/RunScreen.{h,cpp}` `MenuScreen.{h,cpp}` `EditValueScreen.{h,cpp}` | Concrete screens. |
| `src/Settings.h` / `.cpp` | Persistent settings struct + EEPROM load/save. |

---

## Task 1: Input event layer

Refactor the existing inline `readControls()` into an `Input` module that emits discrete events, decoupling physical wiring from meaning.

**Files:**
- Create: `src/Config.h`, `src/Input.h`, `src/Input.cpp`
- Modify: `src/OScope.ino` (use `Input`, show last event for verification)

**Interfaces:**
- Produces:
  ```cpp
  // Config.h
  #define TFT_SCLK 13
  #define TFT_MOSI 11
  #define TFT_DC   10
  #define TFT_CS    9
  #define TFT_RST   8
  #define SW_ENC   21
  #define ENC_A    20
  #define ENC_B    19
  #define BTN_MODE 18   // B1 (was SW1)
  #define BTN_CHAN 15   // B2 (was SW2)
  #define BTN_RUN  14   // B3 (was SW3)
  #define LED1 2
  #define LED2 3
  #define LED3 4
  #define SIGNAL_A A2
  #define SIGNAL_B A3
  #define LONG_PRESS_MS 500

  // Input.h
  enum class Btn : uint8_t { Mode, Channel, RunStop, Encoder, COUNT };
  enum class EventType : uint8_t { None, ShortPress, LongPress, EncoderTurn };
  struct InputEvent {
    EventType type = EventType::None;
    Btn       button = Btn::Mode; // valid for Short/LongPress
    int8_t    delta = 0;          // valid for EncoderTurn (signed detents)
  };
  class Input {
   public:
    void begin();
    // Pops the next pending event; returns false when none remain.
    bool poll(InputEvent& out);
  };
  ```

- [ ] **Step 1: Create `Config.h`** with the defines above (move pin defines out of `OScope.ino`).

- [ ] **Step 2: Create `Input.h`** with the interface above plus private debounce/long-press state per button and an `Encoder` member, and a small fixed-size event FIFO (`InputEvent _queue[8]`, head/tail).

- [ ] **Step 3: Implement `Input.cpp`.**

```cpp
#include "Input.h"
#include "Config.h"
#include <Encoder.h>

// index order MUST match Btn: Mode,Channel,RunStop,Encoder
static const uint8_t BTN_PIN[] = {BTN_MODE, BTN_CHAN, BTN_RUN, SW_ENC};

static Encoder s_encoder(ENC_A, ENC_B);
static long s_lastEnc = 0;

struct BtnState { bool rawLast=false; bool stable=false; uint32_t change=0; bool longFired=false; uint32_t downAt=0; };
static BtnState s_btn[(int)Btn::COUNT];
static InputEvent s_q[8]; static uint8_t s_head=0, s_tail=0;
static void qpush(const InputEvent& e){ uint8_t n=(s_tail+1)&7; if(n!=s_head){ s_q[s_tail]=e; s_tail=n; } }

void Input::begin() {
  for (uint8_t i=0;i<(int)Btn::COUNT;i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
  s_lastEnc = s_encoder.read();
}

bool Input::poll(InputEvent& out) {
  uint32_t now = millis();
  // Encoder: emit one EncoderTurn per detent of accumulated motion.
  long pos = s_encoder.read() / 4;
  long start = s_lastEnc / 4;
  if (pos != start) {
    InputEvent e; e.type=EventType::EncoderTurn; e.delta=(int8_t)(pos-start);
    qpush(e); s_lastEnc = s_encoder.read();
  }
  // Buttons: debounce + short(on release)/long(at threshold).
  for (uint8_t i=0;i<(int)Btn::COUNT;i++) {
    bool raw = (digitalRead(BTN_PIN[i])==LOW);
    BtnState& b = s_btn[i];
    if (raw != b.rawLast) { b.rawLast=raw; b.change=now; }
    if (now - b.change >= 10) {            // debounce settle
      if (raw && !b.stable) { b.stable=true; b.downAt=now; b.longFired=false; }
      else if (!raw && b.stable) {         // release
        b.stable=false;
        if (!b.longFired) { InputEvent e; e.type=EventType::ShortPress; e.button=(Btn)i; qpush(e); }
      }
    }
    if (b.stable && !b.longFired && now - b.downAt >= LONG_PRESS_MS) {
      b.longFired=true; InputEvent e; e.type=EventType::LongPress; e.button=(Btn)i; qpush(e);
    }
  }
  if (s_head==s_tail) return false;
  out = s_q[s_head]; s_head=(s_head+1)&7; return true;
}
```

- [ ] **Step 4: Rewrite `OScope.ino` loop** to drain events and show the most recent one (temporary verification UI), keeping the existing framebuffer setup:

```cpp
Input input;
char lastEvent[32] = "none";
void setup(){ /* serial, tft.begin, rotation, setFrameBuffer, useFrameBuffer */ input.begin(); }
void loop(){
  InputEvent e;
  while (input.poll(e)) {
    if (e.type==EventType::EncoderTurn) snprintf(lastEvent,sizeof lastEvent,"enc %+d", e.delta);
    else if (e.type==EventType::ShortPress) snprintf(lastEvent,sizeof lastEvent,"short b%d",(int)e.button);
    else if (e.type==EventType::LongPress)  snprintf(lastEvent,sizeof lastEvent,"LONG  b%d",(int)e.button);
  }
  tft.fillScreen(0); tft.setTextSize(2); tft.setCursor(40,110); tft.setTextColor(0xFFFF);
  tft.print(lastEvent); tft.updateScreen();
}
```

- [ ] **Step 5: Build.** Run `just build` → expect `[SUCCESS]`.

- [ ] **Step 6: Flash & verify on hardware.** Run `just run`. Confirm: each button shows `short b0..b2` (Mode/Chan/Run) and `b3` for the encoder press; holding ≥0.5 s shows `LONG`; turning the encoder shows `enc +1` / `enc -1` per detent, correct sign. No double-fires (debounce).

- [ ] **Step 7: Commit.** `git add src/ && git commit -m "feat: input event layer (debounced buttons + encoder events)"`

---

## Task 2: Theme, Renderer, Screen stack, and a RunScreen skeleton

Stand up the navigation framework and a run screen that reacts to events with stubbed state (no real signal yet).

**Files:**
- Create: `src/Theme.h`, `src/Renderer.h/.cpp`, `src/ScopeState.h/.cpp`, `src/screens/Screen.h`, `src/screens/RunScreen.h/.cpp`
- Modify: `src/OScope.ino`

**Interfaces:**
- Consumes: `InputEvent`, `Input` (Task 1).
- Produces:
  ```cpp
  // ScopeState.h
  enum class Mode : uint8_t { Triggered, Rolling, XY, COUNT };
  enum class ChannelSel : uint8_t { A, B, Both };
  enum class EncoderParam : uint8_t { Timebase, VScale, TriggerLevel };
  struct ScopeState {
    Mode mode = Mode::Triggered;
    ChannelSel channel = ChannelSel::A;
    EncoderParam selected = EncoderParam::Timebase;
    bool running = true;
    uint16_t timebase_us_per_div = 500;
    uint16_t vscale_mv_per_div[2] = {1000, 1000};
    int16_t  trigger_level_mv = 0;
    bool     channelEnabled[2] = {true, true};
    void resetToDefaults();
  };
  const char* modeName(Mode);
  const char* channelName(ChannelSel);

  // Renderer.h
  class Renderer {
   public:
    explicit Renderer(GC9A01A_t3n& t);
    void clear();
    void text(int16_t x,int16_t y,const char* s,uint16_t color,uint8_t size=1);
    void hline(int16_t x,int16_t y,int16_t w,uint16_t c);
    void vline(int16_t x,int16_t y,int16_t h,uint16_t c);
    GC9A01A_t3n& tft;
  };

  // screens/Screen.h
  struct AppContext { ScopeState& state; class ScreenStack& screens; };
  class Screen {
   public:
    virtual ~Screen() {}
    virtual void onEnter(AppContext&) {}
    virtual void onExit(AppContext&) {}
    virtual void handleEvent(const InputEvent&, AppContext&) = 0;
    virtual void draw(Renderer&, AppContext&) = 0;
  };
  class ScreenStack {
   public:
    void reset(Screen* root, AppContext& ctx);  // clear + push root
    void push(Screen* s, AppContext& ctx);
    void pop(AppContext& ctx);                   // no-op if only root remains
    Screen* top();
    void handleEvent(const InputEvent& e, AppContext& ctx);
    void draw(Renderer& r, AppContext& ctx);
   private:
    Screen* _stack[6]; uint8_t _n = 0;
  };
  ```

- [ ] **Step 1: Create `Theme.h`** — move all colors/layout magic numbers here:

```cpp
#pragma once
#include <stdint.h>
namespace Theme {
  constexpr uint16_t Background=0x0000, Grid=0x18E3, Frame=0xFFFF;
  constexpr uint16_t TraceA=0x07E0, TraceB=0x07FF, Text=0xFFFF, Dim=0xC618, Highlight=0xFFE0;
  constexpr int16_t  W=240, H=240, CX=120, CY=120, SafeInset=30;
}
```

- [ ] **Step 2: Create `Renderer.{h,cpp}`** wrapping `tft` with the methods above (thin pass-throughs to `tft.fillScreen`, `setCursor/print`, `drawFastHLine/VLine`).

- [ ] **Step 3: Create `ScopeState.{h,cpp}`.** Implement `resetToDefaults()` (assign the in-class defaults), `modeName()` (`"TRIG"/"ROLL"/"X-Y"`), `channelName()` (`"A"/"B"/"A+B"`).

- [ ] **Step 4: Create `screens/Screen.h`** with the interface + `ScreenStack` above; implement `ScreenStack` inline (push/pop manage `_stack`, call `onEnter/onExit`, `handleEvent`/`draw` delegate to `top()`).

- [ ] **Step 5: Create `screens/RunScreen.{h,cpp}`.** Stub behavior wiring events to state:

```cpp
void RunScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
  auto& s = ctx.state;
  if (e.type==EventType::ShortPress) {
    switch (e.button) {
      case Btn::Mode:    s.mode = (Mode)(((int)s.mode+1)%(int)Mode::COUNT); break;
      case Btn::Channel: s.channel = (ChannelSel)(((int)s.channel+1)%3); break;
      case Btn::RunStop: s.running = !s.running; break;
      case Btn::Encoder: s.selected = (EncoderParam)(((int)s.selected+1)%3); break; // Task 3 makes this context-aware via nextSelectable()
      default: break;
    }
  } else if (e.type==EventType::LongPress && e.button==Btn::Encoder) {
    s.resetToDefaults();
  }
  // Mode long-press (Settings) and Channel/RunStop long-press handled in later tasks.
}
void RunScreen::draw(Renderer& r, AppContext& ctx) {
  auto& s = ctx.state;
  r.clear();
  r.text(90,30, modeName(s.mode), Theme::Text, 2);
  r.text(40,90, "Sel:", Theme::Dim);  r.text(80,90, paramName(s.selected), Theme::Highlight);
  char b[24]; snprintf(b,sizeof b,"Chan: %s", channelName(s.channel));  r.text(40,110,b,Theme::Text);
  r.text(40,130, s.running?"RUN":"STOP", s.running?Theme::TraceA:Theme::Highlight);
}
```
(For now selection cycles all three `EncoderParam` inline; add a static `paramName(EncoderParam)` helper in `RunScreen.cpp` returning `"Time"/"V/div"/"Trig"`. Task 3 replaces both with `nextSelectable()` and `parameterFor().name`.)

- [ ] **Step 6: Rewire `OScope.ino`.** Construct `ScopeState state; Renderer renderer(tft); ScreenStack screens; RunScreen runScreen; AppContext ctx{state, screens};` In `setup()` call `screens.reset(&runScreen, ctx)`. In `loop()`: drain `input.poll` → `screens.handleEvent`; then `screens.draw(renderer, ctx); tft.updateScreen();` plus `countFrame()`.

- [ ] **Step 7: Build** → `just build` → `[SUCCESS]`.

- [ ] **Step 8: Flash & verify.** `just run`. Confirm: B1 cycles `TRIG→ROLL→X-Y`; encoder press cycles `Sel: Time→V/div→Trig`; B2 cycles `Chan: A→B→A+B`; B3 toggles `RUN/STOP`; encoder long-press returns everything to defaults. FPS still smooth.

- [ ] **Step 9: Commit.** `git commit -am "feat: screen stack + theme/renderer + RunScreen skeleton"`

---

## Task 3: Parameter abstraction + live value adjustment

Make the encoder actually change values, with per-mode context-aware selection and formatted readouts.

**Files:**
- Create: `src/Parameter.h/.cpp`
- Modify: `src/screens/RunScreen.cpp`, `src/ScopeState.h` (add nothing structural; helpers only)

**Interfaces:**
- Consumes: `ScopeState`, `EncoderParam`, `Mode`.
- Produces:
  ```cpp
  // Parameter.h
  struct Parameter {
    EncoderParam id;
    const char* name;
    void (*adjust)(ScopeState&, int8_t delta);
    void (*format)(const ScopeState&, char* buf, uint8_t n);
  };
  const Parameter& parameterFor(EncoderParam id);
  bool paramAppliesInMode(EncoderParam id, Mode m);   // Triggered=all; Rolling=Time,VScale; XY=VScale
  EncoderParam nextSelectable(const ScopeState& s);    // cycle, skipping N/A in current mode
  void clampSelectable(ScopeState& s);                 // fix selection if mode change invalidated it
  ```

- [ ] **Step 1: Implement `Parameter.cpp`** with the descriptor table and stepping logic. Example entries:

```cpp
static void adjTime(ScopeState& s, int8_t d){
  static const uint16_t steps[]={50,100,200,500,1000,2000,5000,10000};
  int i = indexOf(steps, s.timebase_us_per_div); i = clampi(i+d, 0, 7);
  s.timebase_us_per_div = steps[i];
}
static void fmtTime(const ScopeState& s, char* b, uint8_t n){
  if (s.timebase_us_per_div>=1000) snprintf(b,n,"%u ms/div", s.timebase_us_per_div/1000);
  else snprintf(b,n,"%u us/div", s.timebase_us_per_div);
}
static void adjVScale(ScopeState& s, int8_t d){
  static const uint16_t steps[]={100,200,500,1000,2000,5000};
  uint8_t lo = (s.channel==ChannelSel::B)?1:0, hi=(s.channel==ChannelSel::A)?0:1;
  for (uint8_t c=lo;c<=hi && c<2;c++){ int i=indexOf(steps,s.vscale_mv_per_div[c]); i=clampi(i+d,0,5); s.vscale_mv_per_div[c]=steps[i]; }
}
static void fmtVScale(const ScopeState& s, char* b, uint8_t n){
  uint8_t c=(s.channel==ChannelSel::B)?1:0; snprintf(b,n,"%u mV/div", s.vscale_mv_per_div[c]);
}
static void adjTrig(ScopeState& s, int8_t d){ s.trigger_level_mv = clampi(s.trigger_level_mv + d*100, -10000, 10000); }
static void fmtTrig(const ScopeState& s, char* b, uint8_t n){ snprintf(b,n,"%d mV", s.trigger_level_mv); }
```

Implement `paramAppliesInMode`, `nextSelectable` (advance `selected` and skip ids where `!paramAppliesInMode`), and `clampSelectable`.

- [ ] **Step 2: Wire into RunScreen.** Replace the stub: on `EncoderTurn`, call `parameterFor(s.selected).adjust(s, e.delta)`. On `Encoder` short-press, `s.selected = nextSelectable(s)`. On `Mode` change, call `clampSelectable(s)`. Draw the selected param's value with its `format()`:

```cpp
char val[24]; parameterFor(s.selected).format(s, val, sizeof val);
r.text(40,150, parameterFor(s.selected).name, Theme::Dim);
r.text(40,170, val, Theme::Highlight);
```

- [ ] **Step 3: Build** → `just build` → `[SUCCESS]`.

- [ ] **Step 4: Flash & verify.** `just run`. Confirm: turning the encoder changes the selected value with correct units; cycling selection in **Rolling** skips Trigger, in **X-Y** lands only on V/div; with Channel=A vs B, V/div edits the right channel; encoder long-press resets values.

- [ ] **Step 5: Commit.** `git commit -am "feat: Parameter abstraction + live value adjustment"`

---

## Task 4: Acquisition + Triggered mode rendering

Sample both channels and draw a real, triggered waveform; scales/trigger now affect the trace.

**Files:**
- Create: `src/modes/ScopeMode.h`, `src/Acquisition.h/.cpp`, `src/modes/TriggeredMode.h/.cpp`
- Modify: `src/screens/RunScreen.cpp` (delegate drawing to the active mode), `src/OScope.ino`

**Interfaces:**
- Consumes: `ScopeState`, `Renderer`.
- Produces:
  ```cpp
  // modes/ScopeMode.h
  struct SampleBuffers {
    static constexpr uint16_t N = 240;
    uint16_t ch[2][N];   // raw 0..1023 ADC counts (10-bit)
    uint16_t count = 0;  // valid samples
  };
  class ScopeMode {
   public:
    virtual ~ScopeMode() {}
    virtual const char* name() const = 0;
    virtual void render(Renderer&, const ScopeState&, const SampleBuffers&) = 0;
  };

  // Acquisition.h
  class Acquisition {
   public:
    void begin();
    void capture(const ScopeState&, SampleBuffers&);   // blocking single sweep
  };
  ```
  Mapping helpers (in `Renderer` or a `Mapping.h`): `int16_t sampleToY(uint16_t adc, uint16_t mv_per_div)`, grid spacing constants.

- [ ] **Step 1: Implement `Acquisition.cpp`.** `begin()` sets `analogReadResolution(10)`. `capture()` reads `SIGNAL_A`/`SIGNAL_B` into `ch[0]/ch[1]` for `N` samples spaced by `timebase_us_per_div`-derived `delayMicroseconds`. (Trigger search added in Step 3.)

- [ ] **Step 2: Implement grid + trace draw** in `TriggeredMode::render`: draw oscilloscope grid (use `Theme::Grid`, division spacing 30 px), then plot each channel by connecting `sampleToY` points with `tft` lines, channel A in `Theme::TraceA`, B in `Theme::TraceB` (respect `channelEnabled`).

- [ ] **Step 3: Add trigger** in `Acquisition::capture` for Triggered mode: pre-scan for a rising crossing of `trigger_level_mv` (converted to ADC counts) on the trigger source channel; align sweep start to it. If none within a window and trigger Mode=Auto (Task 7; default Auto), free-run.

- [ ] **Step 4: Delegate from RunScreen.** Hold a `ScopeMode*` table indexed by `Mode`; in `RunScreen::draw`, after the HUD, call `activeMode->render(r, s, buffers)`. In `loop()` (or RunScreen), when `state.running`, call `acquisition.capture(state, buffers)` each frame.

- [ ] **Step 5: Build** → `just build` → `[SUCCESS]`.

- [ ] **Step 6: Flash & verify with a signal.** Feed an audio/CV signal (function generator or another module) into Channel A. Confirm: waveform is visible and stable (triggered), V/div changes amplitude, timebase changes horizontal span, trigger level shifts the lock point. Channel B plots when present.

- [ ] **Step 7: Commit.** `git commit -am "feat: ADC acquisition + triggered waveform rendering"`

---

## Task 5: Channel select, Run/Stop freeze, single-shot

Finish the run-screen control semantics.

**Files:** Modify `src/screens/RunScreen.cpp`, `src/Acquisition.cpp`, `src/OScope.ino`

**Interfaces:**
- Consumes: Tasks 1–4. Adds `bool ScopeState::singleArmed` (add to struct + defaults = false).

- [ ] **Step 1:** B2 long-press toggles `channelEnabled[active]`; ensure the disabled trace isn't drawn and V/div on a disabled channel is skipped.
- [ ] **Step 2:** B3 short-press toggles `state.running`; when stopped, `loop()` skips `acquisition.capture` so the last `buffers` stay frozen on screen.
- [ ] **Step 3:** B3 long-press sets `singleArmed=true` and `running=true`; after the next successful triggered capture, set `running=false` and `singleArmed=false` (one-shot).
- [ ] **Step 4: Build** → `just build` → `[SUCCESS]`.
- [ ] **Step 5: Flash & verify.** Confirm freeze holds the trace; B2 long-press hides/shows a channel; single-shot captures exactly one triggered frame then stops.
- [ ] **Step 6: Commit.** `git commit -am "feat: channel enable, run/stop freeze, single-shot"`

---

## Task 6: Rolling and X-Y modes

Add the two remaining modes behind the `ScopeMode` interface.

**Files:** Create `src/modes/RollingMode.{h,cpp}`, `src/modes/XYMode.{h,cpp}`; modify the mode table in `RunScreen.cpp` and `Acquisition.cpp`.

- [ ] **Step 1:** `RollingMode::render` scrolls samples right-to-left (ring-buffer write index); acquisition is free-running (no trigger search). Encoder cycle already excludes Trigger via `paramAppliesInMode`.
- [ ] **Step 2:** `XYMode::render` plots Channel A (X) vs Channel B (Y) as points/dots within the circle; V/div scales both axes.
- [ ] **Step 3:** Register both in the mode table so B1 cycles `TRIG→ROLL→X-Y`; verify `clampSelectable` keeps the encoder selection valid on entry.
- [ ] **Step 4: Build** → `just build` → `[SUCCESS]`.
- [ ] **Step 5: Flash & verify.** Confirm Rolling scrolls a slow signal smoothly; X-Y draws a Lissajous from two signals; mode cycling and per-mode encoder selection behave per the table in the design doc.
- [ ] **Step 6: Commit.** `git commit -am "feat: rolling and X-Y modes"`

---

## Task 7: Settings menu

Add the menu screens and the settings the design calls for.

**Files:** Create `src/Settings.h/.cpp`, `src/screens/MenuScreen.{h,cpp}`, `src/screens/EditValueScreen.{h,cpp}`; modify `RunScreen.cpp` (B1 long-press opens menu), `AppContext` (add `Settings& settings`).

**Interfaces:**
```cpp
// Settings.h
enum class TrigSource: uint8_t { A, B };
enum class TrigEdge:   uint8_t { Rising, Falling };
enum class TrigMode:   uint8_t { Auto, Normal };
struct Settings {
  TrigSource trigSource = TrigSource::A;
  TrigEdge   trigEdge   = TrigEdge::Rising;
  TrigMode   trigMode   = TrigMode::Auto;
  uint8_t    brightness = 200;
  bool       grid       = true;
  void defaults();
};
```

- [ ] **Step 1:** Implement `MenuScreen` as a list driven by a static array of menu items (label + action: open submenu, or edit a `Parameter`-like setting). Encoder scrolls (highlight), encoder press enters/edits, B1 pops (`ctx.screens.pop`); at root, B1 pops back to RunScreen.
- [ ] **Step 2:** Implement `EditValueScreen`: shows one value; encoder turn changes it live; encoder press confirms (pop), B1 cancels (pop, restore prior value).
- [ ] **Step 3:** Wire B1 long-press in `RunScreen` to `ctx.screens.push(&menuScreen, ctx)`. Apply `settings.trig*` in `Acquisition`, `settings.grid` in mode render, `settings.brightness` to the display.
- [ ] **Step 4: Build** → `just build` → `[SUCCESS]`.
- [ ] **Step 5: Flash & verify.** Confirm B1 long-press opens Settings; encoder navigates; editing trigger edge/source/mode, brightness, and grid all take effect on the run screen; B1 backs out level-by-level to the scope.
- [ ] **Step 6: Commit.** `git commit -am "feat: settings menu (trigger, display)"`

---

## Task 8: Settings persistence + reset polish

Persist settings across power cycles; confirm reset scope.

**Files:** Modify `src/Settings.cpp` (EEPROM load/save), `src/OScope.ino` (load on boot, save on change).

- [ ] **Step 1:** `Settings::load()`/`save()` using Teensy `EEPROM` (with a magic byte + version for first-boot defaults). Call `load()` in `setup()`; `save()` when a setting edit is confirmed.
- [ ] **Step 2:** Confirm `ScopeState::resetToDefaults()` (encoder long-press) resets only acquisition params/mode/channel/run — NOT `Settings`. Verify the two are independent.
- [ ] **Step 3: Build** → `just build` → `[SUCCESS]`.
- [ ] **Step 4: Flash & verify.** Change brightness/grid/trigger, power-cycle the Teensy, confirm they persist. Confirm encoder long-press resets the trace params but leaves settings intact.
- [ ] **Step 5: Commit.** `git commit -am "feat: settings persistence + reset scoping"`

---

## Deferred (not in this plan)
- LED semantics (undecided — extra feedback).
- Measurements/cursors, FFT, dual-trigger, persistence/intensity display.
- Optional: native `pio test -e native` unit tests for pure logic (debounce, `nextSelectable`, parameter stepping, ScreenStack) — add if logic churn makes hardware testing slow.

## Notes for implementers
- After each task the firmware is a working, flashable build — never leave `main` non-compiling.
- Keep `OScope.ino` thin; new behavior belongs in the module that owns that responsibility (see File Structure table).
- Magic numbers for color/layout go in `Theme.h`; pins/timing in `Config.h`. If you're typing a hex color or a pin number anywhere else, move it.
- When you add a parameter, mode, screen, or setting, follow the existing table/registry pattern — no new control-flow branches in the loop.
