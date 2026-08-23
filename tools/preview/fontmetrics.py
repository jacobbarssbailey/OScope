#!/usr/bin/env python3
"""Per-glyph metrics for the Inter fonts in src/font_Inter.cpp.

Parses the ILI9341_t3 font arrays out of the C source and decodes each glyph's
header exactly as GC9A01A_t3n::drawFontChar and ::strPixelLen do, so layout
maths done here matches what the Teensy actually draws:

  - a string's width is the sum of its glyphs' `delta` advances;
  - the cursor Y passed to Renderer::text() is the TOP OF THE CAP HEIGHT, and a
    glyph's ink starts at `cap_height - height - yoffset` rows below it.

Run it directly to dump metrics for a few probe strings, or pass `<size>:<text>`
arguments to measure your own:

    python3 tools/preview/fontmetrics.py 36:1000 20:persist
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(REPO, "src", "font_Inter.cpp")

# Mirrors the ILI9341_t3_font_t initialisers at the bottom of font_Inter.cpp.
# (name, bits_index, bits_width, bits_height, bits_xoffset, bits_yoffset,
#  bits_delta, line_space, cap_height)
FONTS = {
    14: ("Inter_14_Bold_Italic", 11, 5, 5, 3, 5, 5, 22, 14),
    20: ("Inter_20_Bold_Italic", 12, 5, 6, 4, 6, 5, 33, 20),
    36: ("Inter_36_Bold_Italic", 14, 6, 6, 5, 6, 6, 60, 37),
    42: ("Inter_42_Bold_Italic", 14, 6, 7, 5, 7, 6, 69, 42),
}

_text = None


def _source():
    global _text
    if _text is None:
        _text = open(SRC).read()
    return _text


def array(name):
    """Read one `static const unsigned char <name>[] = {...}` array."""
    m = re.search(r"unsigned char " + name + r"\[\] = \{(.*?)\};", _source(), re.S)
    if not m:
        raise KeyError("array %s not found in %s" % (name, SRC))
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))


def fetch_u(buf, pos, nbits):
    """Big-endian bit fetch, matching the driver's fetchbits_unsigned."""
    v = 0
    for i in range(nbits):
        byte = buf[(pos + i) >> 3]
        v = (v << 1) | ((byte >> (7 - ((pos + i) & 7))) & 1)
    return v


def fetch_s(buf, pos, nbits):
    v = fetch_u(buf, pos, nbits)
    if v & (1 << (nbits - 1)):
        v -= 1 << nbits
    return v


class Font:
    """One packaged size: glyph headers, string widths, ink extents."""

    def __init__(self, size):
        name, bi, bw, bh, bx, by, bd, ls, cap = FONTS[size]
        self.size = size
        self.bits = (bi, bw, bh, bx, by, bd)
        self.line_space = ls
        self.cap_height = cap
        self.data = array(name + "_data")
        self.index = array(name + "_index")

    def header(self, ch):
        """(width, height, xoffset, yoffset, delta, bit position of the pixels)."""
        bi, bw, bh, bx, by, bd = self.bits
        off = fetch_u(self.index, (ord(ch) - 32) * bi, bi)
        d = self.data[off:]
        p = 3  # 3-bit encoding field, always 0 for these fonts
        w = fetch_u(d, p, bw); p += bw
        h = fetch_u(d, p, bh); p += bh
        xo = fetch_s(d, p, bx); p += bx
        yo = fetch_s(d, p, by); p += by
        delta = fetch_u(d, p, bd); p += bd
        return d, w, h, xo, yo, delta, p

    def glyph(self, ch):
        _, w, h, xo, yo, delta, _ = self.header(ch)
        return dict(w=w, h=h, xo=xo, yo=yo, delta=delta)

    def width(self, s):
        return sum(self.glyph(c)["delta"] for c in s)

    def ink(self, s):
        """(top, bottom) inked rows relative to the cursor Y."""
        top, bot = 999, -999
        for c in s:
            g = self.glyph(c)
            if g["h"] == 0:
                continue
            oy = self.cap_height - g["h"] - g["yo"]
            top = min(top, oy)
            bot = max(bot, oy + g["h"])
        return top, bot


def main(argv):
    fonts = {s: Font(s) for s in FONTS}
    if argv:
        for arg in argv:
            size, s = arg.split(":", 1)
            f = fonts[int(size)]
            t, b = f.ink(s)
            print("%spx %r: w=%d ink %d..%d" % (size, s, f.width(s), t, b))
        return
    for size in sorted(FONTS):
        f = fonts[size]
        print("--- %dpx  cap_height=%d line_space=%d" % (size, f.cap_height, f.line_space))
        for probe in ("A", "A#", "0123456789", "Hz", "mv", "trigger", "persist"):
            t, b = f.ink(probe)
            print("   %-12r w=%3d  ink rows %d..%d (h=%d)"
                  % (probe, f.width(probe), t, b, b - t))


if __name__ == "__main__":
    main(sys.argv[1:])
