/*
 * OScope Display Test
 * Basic test sketch for GC9A01A round display with Teensy 4.0
 *
 * Display connections:
 * - SCK:  Pin 20 (13_SCK_CRX1_LED)
 * - SDA:  Pin 13 (14_A0_TX3_SPDIF_OUT) [MOSI]
 * - DC:   Pin 10 (9_OUT1C) - Using hardware CS pin for faster data/command switching
 * - CS:   Pin 9 (6_OUT1A)
 * - RST:  Pin 11 (11_MOSI_CTX1)
 */

#include <Adafruit_GFX.h>
#include <GC9A01A_t3n.h>
#include <SPI.h>

// Pin definitions for GC9A01A display
#define TFT_DC    9
#define TFT_CS    10
#define TFT_RST   8
#define TFT_MOSI  11
#define TFT_SCLK  13

// Display resolution
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240
#define CENTER GC9A01A_t3n::CENTER

// Framebuffers for double buffering
DMAMEM uint16_t fb1[240 * 240];
DMAMEM uint16_t fb2[240 * 240];

// Initialize display with GC9A01A_t3n (optimized for Teensy 4.x)
GC9A01A_t3n tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("OScope Display Test Starting...");

  // Initialize SPI
  SPI.begin();

  // Initialize display
  Serial.println("Initializing GC9A01A display...");
  tft.begin();

  // Set rotation (adjust if needed: 0, 1, 2, or 3)
  tft.setRotation(0);

  Serial.println("Display initialized!");

  tft.setFrameBuffer(fb1);
  tft.useFrameBuffer(true);
  tft.print("Using FB1");
  uint32_t start_time = micros();
  tft.updateScreen();
  Serial.printf("Delta time: %u\n", micros() - start_time);

  tft.setFrameBuffer(fb2);
  tft.fillScreen(BLUE);
  tft.setCursor(CENTER, CENTER);
  tft.setTextColor(RED);
  tft.print("Using FB2");
  start_time = micros();
  tft.updateScreen();


}

uint8_t timer = 0;

void loop() {

  tft.setFrameBuffer(fb1);
  drawTestPattern(timer);
  tft.updateScreen();

  timer += 2;

  tft.setFrameBuffer(fb2);
  drawTestPattern(timer);
  tft.updateScreen();

  timer += 2;

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

  tft.setCursor(50, 220);
  tft.setTextColor(0xFFFF); // WHITE
  tft.setTextSize(1);
  tft.println("Display Test");

 // delay(2000);
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
