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
- SCK: Pin 20 (13_SCK_CRX1_LED)
- MOSI/SDA: Pin 13 (14_A0_TX3_SPDIF_OUT)
- DC: Pin 10 (9_OUT1C) - Hardware CS pin for faster data/command switching
- CS: Pin 9 (6_OUT1A)
- RST: Pin 11 (11_MOSI_CTX1)

### Controls
- Rotary Encoder A: ENC_A (Pin 27)
- Rotary Encoder B: ENC_B (Pin 26)
- Encoder Switch: SW_ENC (Pin 28)
- Switch 1: SW1 (Pin 25)
- Switch 2: SW2 (Pin 24)
- Signal A Button: SIGNAL_A (Pin 21)
- Signal B Button: SIGNAL_B (Pin 22)

### LEDs
- LED 1: Pin 1 (0_RX1_CRX2_CS1)
- LED 2: Pin 5 (3_LRCLK2)
- LED 3: Pin 6 (4_BCLK2)

### Analog Inputs
- Channel A: XTRA1 (Pin 2)
- Channel B: XTRA2 (Pin 3)

## Firmware

Location: `src/OScope.ino`

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
