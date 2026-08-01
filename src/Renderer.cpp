// Renderer.cpp — Implementation of the Renderer drawing helper.
//
// Text methods use anti-aliased t3 fonts; the framebuffer is owned by
// OScope.ino and Renderer never calls updateScreen().

#include "Renderer.h"
#include "Theme.h"

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
