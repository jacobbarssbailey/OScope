/*
 * OScope Display Test
 * Basic test sketch for GC9A01A round display with Teensy 4.0
 *
 * Pin assignments below match README.md "Pin Mapping" (Arduino Digital numbers).
 *
 * Display (GC9A01A):
 * - SCK:  Digital 13
 * - SDA:  Digital 11  [MOSI]
 * - DC:   Digital 12
 * - CS:   Digital 9
 * - RST:  Digital 8
 *
 * This sketch is a hardware bring-up test: it renders an oscilloscope-style
 * pattern with a live FPS readout and fades the three indicator LEDs in and
 * out so the display and LEDs can be verified at once.
 */

#include <Adafruit_GFX.h>
#include <GC9A01A_t3n.h>
#include <SPI.h>

// ---- Display (GC9A01A) ----
#define TFT_SCLK  13
#define TFT_MOSI  11
#define TFT_DC    12
#define TFT_CS    9
#define TFT_RST   8

// ---- Controls ----
#define SW_ENC    21  // Encoder push switch
#define ENC_A     20  // Rotary encoder A
#define ENC_B     19  // Rotary encoder B
#define SW1       18
#define SW2       15
#define SW3       14

// ---- Indicator LEDs (PWM-capable) ----
#define LED1      2
#define LED2      3
#define LED3      4

// ---- Analog inputs ----
#define SIGNAL_A  A2  // Channel A (Digital 16)
#define SIGNAL_B  A3  // Channel B (Digital 17)

// Display resolution
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
#define CENTER GC9A01A_t3n::CENTER

// Framebuffers for double buffering
DMAMEM uint16_t fb1[240 * 240];
DMAMEM uint16_t fb2[240 * 240];

// Initialize display with GC9A01A_t3n (optimized for Teensy 4.x)
GC9A01A_t3n tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

// FPS tracking
uint32_t frameCount = 0;
uint32_t lastFpsTime = 0;
float fps = 0.0f;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("OScope Display Test Starting...");

  // Configure LEDs as outputs
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  // Initialize SPI
  SPI.begin();

  // Initialize display
  Serial.println("Initializing GC9A01A display...");
  tft.begin();

  // Set rotation (adjust if needed: 0, 1, 2, or 3)
  tft.setRotation(0);

  Serial.println("Display initialized!");

  lastFpsTime = millis();
}

uint8_t timer = 0;

void loop() {
  tft.setFrameBuffer(fb1);
  drawTestPattern(timer);
  tft.updateScreen();
  countFrame();
  timer += 2;

  tft.setFrameBuffer(fb2);
  drawTestPattern(timer);
  tft.updateScreen();
  countFrame();
  timer += 2;

  updateLeds();
}

// Count a rendered frame and recompute FPS roughly once per second.
void countFrame() {
  frameCount++;
  uint32_t now = millis();
  uint32_t elapsed = now - lastFpsTime;
  if (elapsed >= 1000) {
    fps = (frameCount * 1000.0f) / elapsed;
    frameCount = 0;
    lastFpsTime = now;
  }
}

// Fade all three LEDs in and out using a triangle wave, staggered in phase
// so each LED can be distinguished during the test.
void updateLeds() {
  uint32_t ms = millis();
  analogWrite(LED1, triangle(ms, 0));
  analogWrite(LED2, triangle(ms, 682));    // 1/3 period offset (2000ms / 3)
  analogWrite(LED3, triangle(ms, 1365));    // 2/3 period offset
}

// Triangle wave 0..255 with a 2000ms period and an optional phase offset (ms).
uint8_t triangle(uint32_t ms, uint32_t offset) {
  const uint32_t period = 2000;
  uint32_t phase = (ms + offset) % period;
  uint32_t half = period / 2;
  uint32_t level = (phase < half) ? phase : (period - phase);  // 0..half
  return (uint8_t)((level * 255) / half);
}

void drawTestPattern(uint8_t t) {
  // Fill background
  tft.fillScreen(0x0000); // BLACK

  // Draw outer circle (display boundary)
  tft.drawCircle(120, 120, 119, 0xFFFF); // WHITE

  // Draw grid lines (oscilloscope-style)
  for (int i = 0; i < SCREEN_WIDTH; i += 30) {
    tft.drawFastVLine(i, 0, SCREEN_HEIGHT, 0x18E3); // Dark gray
  }
  for (int i = 0; i < SCREEN_HEIGHT; i += 30) {
    tft.drawFastHLine(0, i, SCREEN_WIDTH, 0x18E3); // Dark gray
  }

  // Draw center crosshairs
  tft.drawFastHLine(0, t, SCREEN_WIDTH, 0x07E0); // GREEN
  tft.drawFastVLine(t, 0, SCREEN_HEIGHT, 0x07E0); // GREEN

  // Draw text
  tft.setCursor(70, 10);
  tft.setTextColor(0x07FF); // CYAN
  tft.setTextSize(2);
  tft.println("OScope");

  // FPS readout
  tft.setCursor(95, 35);
  tft.setTextColor(0xFFE0); // YELLOW
  tft.setTextSize(1);
  tft.printf("FPS: %.1f", fps);

  tft.setCursor(50, 220);
  tft.setTextColor(0xFFFF); // WHITE
  tft.setTextSize(1);
  tft.println("Display Test");
}

// Color wheel function for smooth color transitions
uint16_t colorWheel(uint8_t pos) {
  pos = 255 - pos;
  if (pos < 85) {
    return tft.color565(255 - pos * 3, 0, pos * 3);
  }
  if (pos < 170) {
    pos -= 85;
    return tft.color565(0, pos * 3, 255 - pos * 3);
  }
  pos -= 170;
  return tft.color565(pos * 3, 255 - pos * 3, 0);
}
