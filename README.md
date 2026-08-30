# OScope

A Teensy 4.0 dual-channel oscilloscope in a Eurorack module: two ±10 V inputs on
a 240×240 round display, driven by three buttons and a rotary encoder.

Six display modes — triggered sweep, rolling, X-Y, FFT spectrum, instrument
tuner and a scrolling spectrogram. The face stays clear of menus: settings
appear over a dimmed trace when you reach for the encoder and clear themselves
two seconds later.

**Using the module?** → **[docs/user-guide.md](docs/user-guide.md)** — the
controls, what each one does in each mode, and every setting.

## Hardware

- **MCU**: Teensy 4.0
- **Display**: 240×240 round, GC9A01A driver, mounted at rotation 2
- **Inputs**: 2 DC-coupled channels (±10 V → 0–3.3 V via level shifting)
- **Controls**: 1 rotary encoder with push switch, 3 buttons (active-low, pulled up)
- **Indicators**: 3 LEDs — wired and assigned pins, not yet driven by firmware

## Pin mapping

Pin numbers are Arduino digital pins. `src/Config.h` is the single source of
truth; this table mirrors it, so change both together.

### Display (GC9A01A)
| Signal | Pin |
|---|---|
| SCK | 13 |
| MOSI / SDA | 11 |
| DC | 10 |
| CS | 9 |
| RST | 8 |

### Controls
| Signal | Pin | Role |
|---|---|---|
| `SW_ENC` | 21 | encoder push switch |
| `ENC_A` | 20 | encoder quadrature A |
| `ENC_B` | 19 | encoder quadrature B |
| `BTN_MODE` | 18 | B1 — Mode |
| `BTN_CHAN` | 15 | B2 — Channel |
| `BTN_RUN` | 14 | B3 — Run/Stop |

### Analog inputs
| Signal | Pin | Note |
|---|---|---|
| `SIGNAL_A` | A3 (17) | channel A |
| `SIGNAL_B` | A2 (16) | channel B |

A and B are swapped relative to the obvious ordering to match the physical
wiring — channel A really is on A3.

### LEDs
| Signal | Pin |
|---|---|
| `LED1` | 2 |
| `LED2` | 3 |
| `LED3` | 4 |

### Unused breakouts
`X1` 23/A9 · `X2` 22/A8 · `X3` 5 · `X4` 6 · `X5` 7

## Building

Firmware entry point is `src/OScope.ino`. PlatformIO is the supported
toolchain; `platformio.ini` pins the board, libraries and both environments.

**With [just](https://github.com/casey/just):**

```bash
just build     # pio run — compile
just run       # pio run -t upload — build and flash
just debug     # upload, then open the serial monitor
just test      # pio test -e native — AcqCore unit tests on the host
just preview   # render UI previews to tools/preview/out
```

Run `just` on its own to list every recipe.

**Without just**, the same things are `pio run`, `pio run -t upload`,
`pio test -e native`, and `python3 tools/preview/screens.py`.

Incremental builds take about a second, so build after every change rather than
batching.

### Dependencies

Declared in `platformio.ini` and fetched on first build:

- [GC9A01A_t3n](https://github.com/mjs513/GC9A01A_t3n) — display driver with
  framebuffer support, tuned for Teensy 4.x SPI
- Adafruit GFX Library
- Encoder (PJRC) — interrupt-driven quadrature decoding

ADC, EEPROM and SPI come from the Teensy core and need no declaration.

### Host-side tools

- `python3 tools/preview/screens.py [screen…]` renders each screen to a PNG in
  `tools/preview/out/`, rasterising the real glyph data and scraping the layout
  constants out of the C++ sources. Checking a layout change does not need a
  flash cycle.
- `python3 tools/icons/gen_icons.py` regenerates `src/Icons.cpp` from the art in
  `tools/icons/art/`. Pass `--check` to verify it is up to date. Never hand-edit
  `Icons.cpp`.

Both run on a bare `python3` — no third-party packages.

## Layout

```
src/
  OScope.ino        main loop; owns the framebuffer and the one updateScreen() call
  Acquisition.*     timer-paced ADC -> eDMA capture, trigger search, diagnostics
  RingCapture.*     per-channel sample rings written by DMA, never by the CPU
  Input.*           button/encoder debouncing into an event queue
  Renderer.*        the drawing API screens use
  Theme.h           every colour and layout constant
  Config.h          every pin assignment and compile-time flag
  screens/          RunScreen, MenuScreen, EditValueScreen + the screen stack
  modes/            one strategy per display mode
lib/AcqCore/        hardware-free acquisition logic, unit-tested on the host
test/               native test suites
tools/              preview renderer and icon generator
docs/               user guide, acquisition characterization, display notes
```

`lib/AcqCore/` is the only host-testable part; everything else needs the Teensy
toolchain. See `CLAUDE.md` for the architecture in more depth — the layering
rules, the two acquisition paths, and why the display blit is synchronous.

## Documentation

| | |
|---|---|
| [docs/user-guide.md](docs/user-guide.md) | Operating the module — controls, modes, settings |
| [docs/display-async.md](docs/display-async.md) | Why the blit is synchronous, and what breaks if it isn't |
| [docs/acq-characterization.md](docs/acq-characterization.md) | Bench protocol and results for capture integrity |
| [CLAUDE.md](CLAUDE.md) | Architecture and conventions for working on the firmware |
