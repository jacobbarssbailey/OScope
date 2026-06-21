# OScope

A Teensy 4.0-based dual-channel oscilloscope Eurorack module.

## Hardware

- **MCU**: Teensy 4.0
- **Display**: 240x240 round display with GC9A01A driver
- **Inputs**: 2 DC channels (+/- 10V → 0–3.3V via level shifting)
- **Controls**:
  - 1 rotary encoder
  - 4 buttons with pullup resistors
  - 3 indicator LEDs

## Pin Mapping

### Display (GC9A01A)
- SCK: Pin 20 (Arduino Digital 13)
- MOSI/SDA: Pin 13 (Arduino Digital 11)
- DC: Pin 12 (Arduino Digital 10)
- CS: Pin 11 (Arduino Digital 9)
- RST: Pin 10 (Arduino Digital 8)

### Controls
- Encoder Switch: SW_ENC (Pin 28, Arduino Digital 21)
- Rotary Encoder A: ENC_A (Pin 27, Arduino Digital 20)
- Rotary Encoder B: ENC_B (Pin 26, Arduino Digital 19)
- Switch 1: SW1 (Pin 25, Arduino Digital 18)
- Switch 2: SW2 (Pin 22, Arduino Digital 15)
- Switch 3: SW3 (Pin 21, Arduino Digital 14)

### LEDs
- LED 1: Pin 1 (Pin 4, Arduino Digital 2)
- LED 2: Pin 5 (Pin 5, Arduino Digital 3)
- LED 3: Pin 6 (Pin 6, Arduino Digital 4)

### Analog Inputs
- Channel A: SIGNAL_A (Pin 23, Arduino Digital 16 / Analog 2)
- Channel B: SIGNAL_B (Pin 24, Arduino Digital 17 / Analog 3)

### Unusued Breakouts
- X1: Pin 30, Arduino Digital 23 / Analog 9
- X2: Pin 29, Arduino Digital 22 / Analog 8
- X3: Pin 7, Arduino Digital 5
- X4: Pin 8, Arduino Digital 6
- X5: Pin 9, Arduino Digital 7


## Firmware

Location: `src/OScope.ino`

**Quick commands ([just](https://github.com/casey/just)):**
```bash
just build   # Compile the firmware (pio run)
just run     # Build and upload to the Teensy (pio run -t upload)
just debug   # Upload, then open the serial monitor
```
Run `just` on its own to list available recipes.

**Required Libraries:**
- Adafruit GFX Library
- GC9A01A_t3n Library (https://github.com/mjs513/GC9A01A_t3n)

**Build & Upload (PlatformIO - Recommended):**
```bash
# Build
pio run

# Upload to Teensy
pio run -t upload

# Upload and monitor serial
pio run -t upload && pio device monitor
```

**Build & Upload (Arduino IDE):**
1. Install Adafruit GFX Library via Library Manager
2. Install GC9A01A_t3n from GitHub: https://github.com/mjs513/GC9A01A_t3n
   - Download as ZIP and add via Sketch → Include Library → Add .ZIP Library
3. Open `src/OScope.ino` in Arduino IDE
4. Select board: Tools → Board → Teensy 4.0
5. Select USB Type: Tools → USB Type → Serial
6. Upload to Teensy

**Performance Benefits:**
The GC9A01A_t3n library offers significant performance improvements on Teensy 4.x:
- Optimized SPI communication for Teensy 4.x hardware
- Frame buffer support for flicker-free updates
- Asynchronous DMA updates for non-blocking graphics
- Auto-detection of SPI bus based on pin configuration

**Expected Behavior:**
- Displays oscilloscope-style grid with crosshairs
- Shows "OScope" title text
- Animated color-changing circle in center

## Development Status

- [x] Basic display driver test
- [ ] ADC input reading
- [ ] Rotary encoder input
- [ ] Button handling
- [ ] Waveform rendering
- [ ] Trigger logic
- [ ] UI/menu system
