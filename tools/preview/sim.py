#!/usr/bin/env python3
"""A host-side stand-in for the display, Renderer and Theme.

Rasterises the real Inter glyph data from src/font_Inter.cpp with the same 1bpp
RLE decoder GC9A01A_t3n uses, and mirrors the Renderer primitives the firmware
draws with, so a screen laid out here lands on the same pixels the panel does.

Only text and simple shapes are modelled — enough to check layout. Nothing here
talks to hardware; it exists so UI positions can be checked against design
mockups without a flash cycle.
"""
import math
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fontmetrics import Font, FONTS, fetch_u, fetch_s   # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# ---- Theme.h, as RGB888 equivalents of the RGB565 constants ----
W = H = 240
CX = CY = 120

BG       = (0x00, 0x00, 0x00)
GRID     = (0x18, 0x1C, 0x18)
TRACE_A  = (0xFF, 0x03, 0xEA)
TRACE_B  = (0x5B, 0x6C, 0xED)
TEXT     = (0xFF, 0xFF, 0xFF)
DIM      = (0x8E, 0x8E, 0x8E)
DIM_DARK = (0x40, 0x40, 0x40)
HILITE   = TRACE_A


class Glyphs:
    """Decodes the PJRC 1bpp run-length glyph bitmaps."""

    def __init__(self, size):
        self.f = Font(size)

    def bitmap(self, ch):
        d, w, h, xo, yo, delta, p = self.f.header(ch)
        rows = []
        linecount = h
        while linecount > 0:
            # A leading 1 bit means "repeat this line n times" (n = 3 bits + 2).
            n = 1
            if fetch_u(d, p, 1):
                p += 1
                n = fetch_u(d, p, 3) + 2
                p += 3
            else:
                p += 1
            row, x = [], 0
            while x < w:                     # pixels come in <=32-bit chunks
                xs = min(32, w - x)
                bits = fetch_u(d, p, xs)
                p += xs
                row += [(bits >> (xs - 1 - k)) & 1 for k in range(xs)]
                x += xs
            rows += [row] * n
            linecount -= n
        return w, h, xo, yo, delta, rows[:h]


class Canvas:
    """The framebuffer plus the Renderer methods screens draw through."""

    def __init__(self):
        self.px = [[BG] * W for _ in range(H)]
        self.fonts = {s: Glyphs(s) for s in FONTS}

    def set(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = c

    # ---- shapes ----
    def hline(self, x, y, w, c):
        for i in range(w):
            self.set(x + i, y, c)

    def vline(self, x, y, h, c):
        for i in range(h):
            self.set(x, y + i, c)

    def fill_rect(self, x, y, w, h, c):
        for j in range(h):
            for i in range(w):
                self.set(x + i, y + j, c)

    def line(self, x0, y0, x1, y1, c):
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for s in range(steps + 1):
            self.set(x0 + (x1 - x0) * s // steps, y0 + (y1 - y0) * s // steps, c)

    @staticmethod
    def _circle_octants(r):
        """Bresenham circle offsets, as Adafruit_GFX generates them."""
        pts, f, ddx, ddy, x, y = [], 1 - r, 1, -2 * r, 0, r
        pts += [(0, r), (0, -r), (r, 0), (-r, 0)]
        while x < y:
            if f >= 0:
                y -= 1; ddy += 2; f += ddy
            x += 1; ddx += 2; f += ddx
            pts += [(x, y), (-x, y), (x, -y), (-x, -y),
                    (y, x), (-y, x), (y, -x), (-y, -x)]
        return pts

    def _corner(self, x, y, w, h, r, dx, dy):
        cx = (x + r + dx) if dx < 0 else (x + w - 1 - r + dx)
        cy = (y + r + dy) if dy < 0 else (y + h - 1 - r + dy)
        return cx, cy

    def fill_round_rect(self, x, y, w, h, r, c):
        self.fill_rect(x + r, y, w - 2 * r, h, c)
        self.fill_rect(x, y + r, w, h - 2 * r, c)
        for dx, dy in self._circle_octants(r):
            _, cy = self._corner(x, y, w, h, r, dx, dy)
            lx = x + r if dx >= 0 else x + r + dx
            rx = x + w - 1 - r + dx if dx >= 0 else x + r
            self.hline(min(lx, rx), cy, abs(rx - lx) + 1, c)

    def draw_round_rect(self, x, y, w, h, r, c):
        self.hline(x + r, y, w - 2 * r, c)
        self.hline(x + r, y + h - 1, w - 2 * r, c)
        self.vline(x, y + r, h - 2 * r, c)
        self.vline(x + w - 1, y + r, h - 2 * r, c)
        for dx, dy in self._circle_octants(r):
            self.set(*self._corner(x, y, w, h, r, dx, dy), c)

    # ---- text (y is the top of the cap height, as on the device) ----
    def text(self, x, y, s, c, size):
        g = self.fonts[size]
        cap = g.f.cap_height
        for ch in s:
            w, h, xo, yo, delta, rows = g.bitmap(ch)
            oy, ox = y + cap - h - yo, x + xo
            for j, row in enumerate(rows):
                for i, bit in enumerate(row):
                    if bit:
                        self.set(ox + i, oy + j, c)
            x += delta
        return x

    def width(self, s, size):
        return self.fonts[size].f.width(s)

    def text_center(self, y, s, c, size):
        self.text(CX - self.width(s, size) // 2, y, s, c, size)

    # Renderer::textUnit* — a smaller unit dropped onto the main baseline.
    UNIT_GAP = 2

    def text_unit_width(self, main, unit, size, usize):
        w = self.width(main, size)
        if unit:
            w += self.UNIT_GAP + self.width(unit, usize)
        return w

    def text_unit(self, x, y, main, unit, c, size, usize):
        mw = self.width(main, size)
        self.text(x, y, main, c, size)
        if unit:
            drop = self.fonts[size].f.cap_height - self.fonts[usize].f.cap_height
            self.text(x + mw + self.UNIT_GAP, y + drop, unit, c, usize)

    def text_unit_center(self, y, main, unit, c, size, usize):
        w = self.text_unit_width(main, unit, size, usize)
        self.text_unit(CX - w // 2, y, main, unit, c, size, usize)

    # Fraction of each dash slot that is inked, as a percentage (Renderer.cpp).
    DASH_DUTY = 55

    def ring(self, r, thickness, c, dashes=0):
        cx, cy = (W - 1) * 0.5, (H - 1) * 0.5
        steps = int(4.0 * math.pi * r)
        for i in range(steps):
            if dashes and (i * dashes * 100 // steps) % 100 >= self.DASH_DUTY:
                continue
            t = 2.0 * math.pi * i / steps
            ct, st = math.cos(t), math.sin(t)
            for k in range(thickness):
                rr = r - k
                self.set(int(round(cx + rr * ct)), int(round(cy + rr * st)), c)

    def icon(self, x, y, ic, tint):
        w, h, gray = ic
        for j in range(h):
            for i in range(w):
                v = gray[j * w + i]
                if v:
                    self.set(x + i, y + j, tuple(k * v // 255 for k in tint))

    # ---- output ----
    def save(self, path, scale=2, bezel=(18, 18, 20)):
        """Write a PNG, masking outside the round bezel so the crop is honest."""
        rows = []
        for y in range(H):
            row = []
            for x in range(W):
                dx, dy = x - 119.5, y - 119.5
                inside = dx * dx + dy * dy <= 120 * 120
                row.append(self.px[y][x] if inside else bezel)
            rows.append(row)

        raw = b""
        for y in range(H):
            line = b"\x00"
            for x in range(W):
                line += bytes(rows[y][x]) * scale
            raw += line * scale

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", struct.pack(">IIBBBBB", W * scale, H * scale,
                                            8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw, 9))
               + chunk(b"IEND", b""))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "wb").write(png)


def load_icons():
    """Read the coverage masks back out of src/Icons.cpp, keyed by name."""
    src = open(os.path.join(REPO, "src", "Icons.cpp")).read()
    icons = {}
    for m in re.finditer(r"const Icon Icon(\w+) = \{ (\d+), (\d+), kIcon\w+ \};", src):
        name, w, h = m.group(1), int(m.group(2)), int(m.group(3))
        body = re.search(r"kIcon" + name + r"\[\d+\] = \{(.*?)\};", src, re.S).group(1)
        icons[name] = (w, h, [int(v) for v in re.findall(r"\d+", body)])
    return icons


ICONS = load_icons()
