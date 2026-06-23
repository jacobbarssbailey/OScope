// Renderer.h — Thin drawing helper wrapping the GC9A01A_t3n display driver.
//
// Renderer does NOT own the framebuffer and does NOT call updateScreen().
// The main loop calls tft.updateScreen() exactly once per frame after all
// screens have finished drawing.  Screens draw exclusively through Renderer.
#pragma once

#include <GC9A01A_t3n.h>
#include <stdint.h>

class Renderer {
public:
    explicit Renderer(GC9A01A_t3n& t);

    // Fill the framebuffer with the background color.
    void clear();

    // Draw a string at (x, y) with the given RGB565 color and text size.
    void text(int16_t x, int16_t y, const char* s, uint16_t color, uint8_t size = 1);

    // Draw a horizontal line of width w starting at (x, y).
    void hline(int16_t x, int16_t y, int16_t w, uint16_t c);

    // Draw a vertical line of height h starting at (x, y).
    void vline(int16_t x, int16_t y, int16_t h, uint16_t c);

    // Draw an arbitrary line from (x0, y0) to (x1, y1).
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // Direct reference to the underlying driver (for advanced use by screens).
    GC9A01A_t3n& tft;
};
