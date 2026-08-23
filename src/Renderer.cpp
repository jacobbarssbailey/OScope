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

// Blend `src` over `dst` at coverage 0..31.  Both RGB565 colours are spread
// into one 32-bit word so a single multiply interpolates all three channels at
// once — the whole blend is nine instructions on the M7.
static inline uint16_t blend565(uint16_t dst, uint16_t src, uint8_t cov) {
    const uint32_t bg = (uint32_t)(dst | (dst << 16)) & 0x07E0F81FU;
    const uint32_t fg = (uint32_t)(src | (src << 16)) & 0x07E0F81FU;
    const uint32_t o  = ((((fg - bg) * cov) >> 5) + bg) & 0x07E0F81FU;
    return (uint16_t)((o >> 16) | o);
}

void Renderer::ring(int16_t r, int16_t thickness, uint16_t color, uint8_t dashes) {
    if (r <= 0 || thickness <= 0) return;

    uint16_t* fb = tft.getFrameBuffer();
    if (fb == nullptr) return;   // antialiasing needs to read back what is there

    // The true centre of a 240 px face falls between pixels, so the ring is
    // measured from the half-pixel centre — otherwise it sits a pixel off to
    // one side and the asymmetry is visible against the bezel.
    const float cx   = (Theme::W - 1) * 0.5f;
    const float cy   = (Theme::H - 1) * 0.5f;
    const float rOut = (float)r;
    const float rIn  = (float)(r - thickness);

    // Coverage per pixel: the signed distance into the annulus, taken across
    // one pixel.  A circle drawn by nearest-pixel stepping stair-steps badly at
    // this radius; blending the fringe costs a handful of extra instructions on
    // roughly the ring's perimeter and removes it entirely.
    const float outFringe = rOut + 1.0f;
    const float inFringe  = (rIn > 1.0f) ? rIn - 1.0f : 0.0f;

    int16_t yLo = (int16_t)(cy - outFringe), yHi = (int16_t)(cy + outFringe + 1.0f);
    if (yLo < 0) yLo = 0;
    if (yHi > Theme::H - 1) yHi = Theme::H - 1;

    for (int16_t y = yLo; y <= yHi; ++y) {
        const float dy  = (float)y - cy;
        const float dy2 = dy * dy;
        const float outSpan2 = outFringe * outFringe - dy2;
        if (outSpan2 <= 0.0f) continue;
        const float xOut = sqrtf(outSpan2);

        // Rows that clear the hole in the middle are one span; rows that cross
        // it are two, and skipping the middle is what keeps this O(perimeter).
        const float inSpan2 = inFringe * inFringe - dy2;
        const float xIn = (inSpan2 > 0.0f) ? sqrtf(inSpan2) : -1.0f;

        for (int pass = 0; pass < 2; ++pass) {
            int16_t xa, xb;
            if (xIn < 0.0f) {
                if (pass) break;                       // single span
                xa = (int16_t)(cx - xOut);
                xb = (int16_t)(cx + xOut + 1.0f);
            } else if (pass == 0) {
                xa = (int16_t)(cx - xOut);
                xb = (int16_t)(cx - xIn + 1.0f);
            } else {
                xa = (int16_t)(cx + xIn);
                xb = (int16_t)(cx + xOut + 1.0f);
            }
            if (xa < 0) xa = 0;
            if (xb > Theme::W - 1) xb = Theme::W - 1;

            for (int16_t x = xa; x <= xb; ++x) {
                const float dx = (float)x - cx;
                const float d  = sqrtf(dx * dx + dy2);
                float cov = (rOut - d < d - rIn) ? (rOut - d) : (d - rIn);
                cov += 0.5f;                            // pixel centre to edge
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
                if (dashes) {
                    // Each dash occupies the first kDashDuty% of its slot.
                    float a = atan2f(dy, dx) * (float)(0.5 / M_PI);
                    a -= floorf(a);
                    const float slot = a * (float)dashes;
                    if ((slot - floorf(slot)) * 100.0f >= (float)kDashDuty) continue;
                }
                uint16_t* p = &fb[(int)y * Theme::W + x];
                // Full coverage is stored outright: the blend interpolates in
                // 32nds, so blending at "31" would leave every solid pixel one
                // LSB short of the colour asked for.  It is also the cheaper
                // path, and most of a 2 px ring is fully covered.
                if (cov >= 1.0f) *p = color;
                else             *p = blend565(*p, color, (uint8_t)(cov * 31.0f));
            }
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
