// Renderer.h — Thin drawing helper wrapping the GC9A01A_t3n display driver.
//
// Renderer does NOT own the framebuffer and does NOT call updateScreen().
// The main loop calls tft.updateScreen() exactly once per frame after all
// screens have finished drawing.  Screens draw exclusively through Renderer.
//
// Text is rendered with anti-aliased t3 fonts (see Fonts.h) — callers pass the
// font (e.g. FONT_BODY) rather than a size multiplier.
#pragma once

#include <GC9A01A_t3n.h>   // defines GC9A01A_t3n and ILI9341_t3_font_t
#include <stdint.h>

struct Icon;

class Renderer {
public:
    explicit Renderer(GC9A01A_t3n& t);

    // Fill the framebuffer with the background color.
    void clear();

    // Fade the whole framebuffer toward the (black) background by keep/256 per
    // RGB565 channel — used instead of clear() for persistence/phosphor display.
    // keep == 256 leaves the frame unchanged; smaller values decay faster.
    // Anything redrawn afterward (grid, trace, HUD) returns to full brightness;
    // pixels not redrawn dim a little each frame, leaving a fading trail.
    void fadeFrame(uint16_t keep);

    // Draw a string with its top-left at (x, y) in the given t3 font.
    void text(int16_t x, int16_t y, const char* s, uint16_t color,
              const ILI9341_t3_font_t& font);

    // Draw a string horizontally centered on the display, top at y.
    void textCenterX(int16_t y, const char* s, uint16_t color,
                     const ILI9341_t3_font_t& font);

    // Pixel width of a string in the given font (for manual layout).
    int16_t textWidth(const char* s, const ILI9341_t3_font_t& font);

    // ---- Value + unit ("1000mv", "A4", "440Hz") ----
    // A large main string with a smaller trailing unit, the two sharing a
    // baseline: the unit is dropped by the difference in cap heights.  Used for
    // every numeric readout in the UI, so they all set the same way.
    // `unitGap` is the horizontal gap between main and unit.
    static constexpr int16_t kUnitGap = 2;

    // Combined width of "<main><unit>" as drawn by textUnit().
    int16_t textUnitWidth(const char* main, const char* unit,
                          const ILI9341_t3_font_t& font,
                          const ILI9341_t3_font_t& unitFont,
                          int16_t unitGap = kUnitGap);

    // Draw "<main><unit>" with the main string's top-left at (x, y).
    void textUnit(int16_t x, int16_t y, const char* main, const char* unit,
                  uint16_t color, const ILI9341_t3_font_t& font,
                  const ILI9341_t3_font_t& unitFont, int16_t unitGap = kUnitGap);

    // Draw "<main><unit>" centered on the display, main's top at y.
    void textUnitCenterX(int16_t y, const char* main, const char* unit,
                         uint16_t color, const ILI9341_t3_font_t& font,
                         const ILI9341_t3_font_t& unitFont,
                         int16_t unitGap = kUnitGap);

    // Draw a horizontal line of width w starting at (x, y).
    void hline(int16_t x, int16_t y, int16_t w, uint16_t c);

    // Draw a vertical line of height h starting at (x, y).
    void vline(int16_t x, int16_t y, int16_t h, uint16_t c);

    // Draw an arbitrary line from (x0, y0) to (x1, y1).
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // Fill a w×h rectangle with its top-left at (x, y).
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

    // Fill / outline a w×h rectangle with corner radius r.
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                       uint16_t color);
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                       uint16_t color);

    // Blit an icon with its top-left at (x, y), tinted: each coverage byte
    // scales `tint`, so white reproduces the source art and any other colour
    // recolours it.  Fully transparent pixels are skipped.
    void icon(int16_t x, int16_t y, const Icon& ic, uint16_t tint);

    // Direct reference to the underlying driver (for advanced use by screens).
    GC9A01A_t3n& tft;
};
