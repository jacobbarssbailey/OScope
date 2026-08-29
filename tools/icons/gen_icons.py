#!/usr/bin/env python3
"""Regenerate src/Icons.cpp from the source PNGs in tools/icons/art/.

    python3 tools/icons/gen_icons.py            # rewrite src/Icons.cpp
    python3 tools/icons/gen_icons.py --check    # verify it is up to date

Each icon becomes an 8-bit coverage mask — one byte per pixel, the art's own
greyscale composited on black (see Icons.h).  Renderer::icon() scales a tint
colour by that byte, so the design's "disabled" variants (the same art at
#8E8E8E instead of white) need no data of their own: drawing the white master
with Theme::Dim reproduces them exactly, which is why only the enabled art is
kept here.

Reads PNGs with a ~50-line decoder rather than Pillow so the tool runs on a
bare python3, like everything else under tools/.
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ART = os.path.join(HERE, "art")
DEST = os.path.join(REPO, "src", "Icons.cpp")

# (C identifier, source PNG) in the order they appear in Icons.cpp.
ICONS = [
    ("B1",       "b-1.png"),
    ("B2",       "b-2.png"),
    ("B3",       "b-3.png"),
    ("Enc",      "b-rotary.png"),
    ("Left",     "left.png"),
    ("Right",    "right.png"),
    ("Timebase", "timebase.png"),
    ("VScale",   "scale.png"),
    ("Trigger",  "trigger.png"),
]

HEADER = """// Icons.cpp — Icon coverage masks, generated from the source PNGs.
//
// Generated data: each array is one byte per pixel (see Icons.h).  Do not
// hand-edit — drop new art in tools/icons/art/ and run tools/icons/gen_icons.py.

#include "Icons.h"
"""


def read_png(path):
    """Decode an 8-bit non-interlaced PNG to (w, h, [[(r,g,b,a), ...], ...])."""
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", path
    p, idat, pal, trns = 8, b"", None, None
    while p < len(d):
        ln, typ = struct.unpack(">I4s", d[p:p + 8])
        p += 8
        chunk, p = d[p:p + ln], p + ln + 4
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            assert depth == 8 and interlace == 0, path
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"PLTE":
            pal = chunk
        elif typ == b"tRNS":
            trns = chunk
        elif typ == b"IEND":
            break

    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * nch
    raw = zlib.decompress(idat)
    rows, prev, q = [], bytearray(stride), 0
    for _ in range(h):
        filt, q = raw[q], q + 1
        line, q = bytearray(raw[q:q + stride]), q + stride
        for i in range(stride):
            a = line[i - nch] if i >= nch else 0
            b = prev[i]
            c = prev[i - nch] if i >= nch else 0
            if filt == 1:
                line[i] = (line[i] + a) & 255
            elif filt == 2:
                line[i] = (line[i] + b) & 255
            elif filt == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif filt == 4:
                # Paeth: pick whichever neighbour the linear predictor is nearest.
                pred = a + b - c
                pa, pb, pc = abs(pred - a), abs(pred - b), abs(pred - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc
                                      else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line

    out = []
    for line in rows:
        row = []
        for x in range(w):
            o = x * nch
            if ctype == 6:
                row.append(tuple(line[o:o + 4]))
            elif ctype == 2:
                row.append((line[o], line[o + 1], line[o + 2], 255))
            elif ctype == 0:
                row.append((line[o],) * 3 + (255,))
            elif ctype == 4:
                row.append((line[o],) * 3 + (line[o + 1],))
            else:
                i = line[o]
                alpha = trns[i] if trns and i < len(trns) else 255
                row.append((pal[3 * i], pal[3 * i + 1], pal[3 * i + 2], alpha))
        out.append(row)
    return w, h, out


def coverage(px):
    """Luma premultiplied by alpha — the art as it lands on a black panel."""
    r, g, b, a = px
    return ((r * 77 + g * 151 + b * 28) >> 8) * a // 255


def emit(name, path):
    w, h, rows = read_png(path)
    body = ["// %s  %dx%d" % (os.path.basename(path), w, h),
            "static const uint8_t kIcon%s[%d] = {" % (name, w * h)]
    for row in rows:
        body.append("    " + ",".join("%3d" % coverage(p) for p in row) + ",")
    body.append("};")
    body.append("const Icon Icon%s = { %d, %d, kIcon%s };" % (name, w, h, name))
    return "\n".join(body)


def main():
    text = HEADER + "\n" + "\n\n".join(
        emit(name, os.path.join(ART, src)) for name, src in ICONS) + "\n"
    if "--check" in sys.argv:
        current = open(DEST).read()
        if current != text:
            sys.exit("%s is stale — rerun tools/icons/gen_icons.py" % DEST)
        print("Icons.cpp is up to date")
        return
    open(DEST, "w").write(text)
    print("wrote %s (%d icons)" % (DEST, len(ICONS)))


if __name__ == "__main__":
    main()
