// Renderer.cpp — Implementation of the Renderer drawing helper.
//
// Text methods use anti-aliased t3 fonts; the framebuffer is owned by
// OScope.ino and Renderer never calls updateScreen().

#include "Renderer.h"
#include "Theme.h"
#include "Icons.h"
#include <math.h>

// Fraction of each dash slot that is inked, as a percentage.
static constexpr int kDashDuty = 55;

Renderer::Renderer(GC9A01A_t3n& t) : tft(t) {}

void Renderer::clear() {
    tft.fillScreen(Theme::Background);
}

void Renderer::fadeFrame(uint16_t keep) {
    uint16_t* fb = tft.getFrameBuffer();
    if (fb == nullptr) return;   // no framebuffer: nothing to fade
    const int n = Theme::W * Theme::H;
    for (int i = 0; i < n; ++i) {
        const uint16_t px = fb[i];
        if (px == 0) continue;   // already background — skip the common case
        uint16_t r = (px >> 11) & 0x1F;
        uint16_t g = (px >> 5) & 0x3F;
        uint16_t b = px & 0x1F;
        r = (uint16_t)((r * keep) >> 8);
        g = (uint16_t)((g * keep) >> 8);
        b = (uint16_t)((b * keep) >> 8);
        fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

void Renderer::text(int16_t x, int16_t y, const char* s, uint16_t color,
                    const ILI9341_t3_font_t& font) {
    tft.setFont(font);
    tft.setTextColor(color);
    tft.setCursor(x, y);
    tft.print(s);
}

void Renderer::textCenterX(int16_t y, const char* s, uint16_t color,
                           const ILI9341_t3_font_t& font) {
    tft.setFont(font);
    const int16_t w = tft.strPixelLen(s);
    tft.setTextColor(color);
    tft.setCursor(Theme::CX - w / 2, y);
    tft.print(s);
}

int16_t Renderer::textWidth(const char* s, const ILI9341_t3_font_t& font) {
    tft.setFont(font);
    return tft.strPixelLen(s);
}

// The t3 font renderer treats the cursor Y as the top of the cap height, so
// dropping the smaller font by the cap-height difference lands both strings on
// the same baseline.
static int16_t unitDrop(const ILI9341_t3_font_t& font,
                        const ILI9341_t3_font_t& unitFont) {
    return (int16_t)(font.cap_height - unitFont.cap_height);
}

int16_t Renderer::textUnitWidth(const char* main, const char* unit,
                                const ILI9341_t3_font_t& font,
                                const ILI9341_t3_font_t& unitFont,
                                int16_t unitGap) {
    int16_t w = textWidth(main, font);
    if (unit && unit[0]) w = (int16_t)(w + unitGap + textWidth(unit, unitFont));
    return w;
}

void Renderer::textUnit(int16_t x, int16_t y, const char* main,
                        const char* unit, uint16_t color,
                        const ILI9341_t3_font_t& font,
                        const ILI9341_t3_font_t& unitFont, int16_t unitGap) {
    const int16_t mw = textWidth(main, font);
    text(x, y, main, color, font);
    if (unit && unit[0])
        text((int16_t)(x + mw + unitGap), (int16_t)(y + unitDrop(font, unitFont)),
             unit, color, unitFont);
}

void Renderer::textUnitCenterX(int16_t y, const char* main, const char* unit,
                               uint16_t color, const ILI9341_t3_font_t& font,
                               const ILI9341_t3_font_t& unitFont,
                               int16_t unitGap) {
    const int16_t w = textUnitWidth(main, unit, font, unitFont, unitGap);
    textUnit((int16_t)(Theme::CX - w / 2), y, main, unit, color, font, unitFont,
             unitGap);
}

void Renderer::hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    tft.drawFastHLine(x, y, w, c);
}

void Renderer::vline(int16_t x, int16_t y, int16_t h, uint16_t c) {
    tft.drawFastVLine(x, y, h, c);
}

void Renderer::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    tft.drawLine(x0, y0, x1, y1, color);
}

void Renderer::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}

void Renderer::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                             int16_t r, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, r, color);
}

void Renderer::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                             int16_t r, uint16_t color) {
    tft.drawRoundRect(x, y, w, h, r, color);
}

void Renderer::ring(int16_t r, int16_t thickness, uint16_t color, uint8_t dashes) {
    if (r <= 0 || thickness <= 0) return;

    // The true centre of a 240 px face falls between pixels, so the ring is
    // walked from the half-pixel centre — otherwise it sits a pixel off to one
    // side and the asymmetry is visible against the bezel.
    const float cx = (Theme::W - 1) * 0.5f;
    const float cy = (Theme::H - 1) * 0.5f;

    // Two steps per pixel of circumference: enough that consecutive samples
    // overlap and the ring comes out continuous at any thickness.
    const int steps = (int)(4.0f * (float)M_PI * (float)r);
    for (int i = 0; i < steps; ++i) {
        // Dash pattern: each dash occupies the first kDashDuty% of its slot.
        if (dashes && ((i * (int)dashes * 100 / steps) % 100) >= kDashDuty) continue;
        const float t = (2.0f * (float)M_PI * (float)i) / (float)steps;
        const float c = cosf(t), s = sinf(t);
        for (int16_t k = 0; k < thickness; ++k) {
            const float rr = (float)(r - k);
            tft.drawPixel((int16_t)lroundf(cx + rr * c),
                          (int16_t)lroundf(cy + rr * s), color);
        }
    }
}

void Renderer::icon(int16_t x, int16_t y, const Icon& ic, uint16_t tint) {
    const uint16_t tr = (tint >> 11) & 0x1F;
    const uint16_t tg = (tint >> 5) & 0x3F;
    const uint16_t tb = tint & 0x1F;
    const uint8_t* p = ic.gray;
    for (uint8_t row = 0; row < ic.h; ++row) {
        for (uint8_t col = 0; col < ic.w; ++col) {
            const uint8_t v = *p++;
            if (v == 0) continue;                  // transparent
            const uint16_t r = (uint16_t)(tr * v / 255);
            const uint16_t g = (uint16_t)(tg * v / 255);
            const uint16_t b = (uint16_t)(tb * v / 255);
            tft.drawPixel((int16_t)(x + col), (int16_t)(y + row),
                          (uint16_t)((r << 11) | (g << 5) | b));
        }
    }
}
