#!/usr/bin/env python3
"""Render a preview PNG of each OScope screen.

    python3 tools/preview/screens.py            # all screens
    python3 tools/preview/screens.py tuner      # just one

Output lands in tools/preview/out/ (gitignored).

Layout numbers are scraped straight out of the firmware sources by `consts()`,
so moving a constant in C++ moves it here too. Only *derived* positions (the
ones written as expressions in C++) are recomputed below — if you change how one
of those is derived, mirror the formula here. What cannot be scraped is the
drawing itself: each screen's draw() is transcribed by hand, so a structural
change to a screen needs the matching change here to stay honest.

Signals are synthetic — this checks layout, not acquisition.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import (Canvas, ICONS, REPO, BG, GRID, TRACE_A, TRACE_B, TEXT, DIM,   # noqa: E402
                 DIM_DARK, HILITE, CX, CY, W, H)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")


def consts(relpath):
    """Scrape `constexpr <type> <name> = <literal>;` from a source file.

    Constants defined as expressions are skipped — recompute those in Python.
    """
    text = open(os.path.join(REPO, relpath)).read()
    out = {}
    for m in re.finditer(r"constexpr\s+\w+\s+(\w+)\s*=\s*(-?\d+)\s*;", text):
        out[m.group(1)] = int(m.group(2))
    return out


T = consts("src/Theme.h")
TU = consts("src/modes/TunerMode.cpp")
MN = consts("src/screens/MenuScreen.cpp")


# ---------------------------------------------------------------- Tuner ----
# Derived in TunerMode.cpp: the readouts centre in the gap between the screen
# edge and their channel's marker.
MARK_TOP_A = TU["kMeterYA"] - TU["kMarkH"] // 2
MARK_BOT_B = TU["kMeterYB"] + TU["kMarkH"] // 2
BIG_YA = (MARK_TOP_A - TU["kBigCapH"]) // 2
BIG_YB = MARK_BOT_B + (H - MARK_BOT_B - TU["kBigCapH"]) // 2


def draw_meter(c, cy, cents, have_marker, color):
    ticks = TU["kMeterTicks"]
    left, centre = CX - TU["kMeterHalf"], ticks // 2
    for i in range(ticks):
        x = left + (2 * TU["kMeterHalf"] * i) // (ticks - 1)
        d = abs(i - centre)
        h = 15 if d == 0 else (11 if d in (2, 4) else 7)
        c.vline(x, cy - h // 2, h, DIM_DARK)
    if not have_marker:
        return
    cc = max(-50, min(50, cents))
    mx = CX + (cc * TU["kMeterHalf"]) // 50
    x, y = mx - TU["kMarkW"] // 2, cy - TU["kMarkH"] // 2
    c.fill_round_rect(x, y, TU["kMarkW"], TU["kMarkH"], TU["kMarkR"], color)
    if abs(cents) <= TU["kInTuneCents"]:
        o = TU["kMarkOutset"]
        c.draw_round_rect(x - o, y - o, TU["kMarkW"] + 2 * o, TU["kMarkH"] + 2 * o,
                          TU["kMarkOutR"], color)


def tuner():
    """Channel A sharp by 31 cents, channel B in tune (outlined marker)."""
    c = Canvas()
    c.text_unit_center(BIG_YA, "A", "4", TEXT, 36, 20)
    draw_meter(c, TU["kMeterYA"], 31, True, TRACE_A)
    draw_meter(c, TU["kMeterYB"], 2, True, TRACE_B)
    c.text_unit_center(BIG_YB, "A#", "3", TEXT, 36, 20)
    return c


# --------------------------------------------------------- Scope + band ----
def draw_grid(c):
    for col in range(1, 8):
        c.vline(col * T["GridDiv"], 0, H, GRID)
    for row in range(1, 8):
        c.hline(0, row * T["GridDiv"], W, GRID)


def band(c, label, value, unit):
    """RunScreen's transient parameter band."""
    c.fill_rect(0, T["BandTopY"] + 1, W, T["BandBotY"] - T["BandTopY"] - 1, BG)
    c.hline(0, T["BandTopY"], W, DIM)
    c.hline(0, T["BandBotY"], W, DIM)
    c.text_center(T["BandLabelY"], label, TEXT, 14)
    c.text_unit_center(T["BandValueY"], value, unit, TEXT, 36, 20)


STOPPED = (0xFF, 0x69, 0x00)


def run_ring(c, armed=False):
    """RunScreen's run-state ring: solid when frozen, dashed while armed."""
    c.ring(T["RunRingR"], T["RunRingW"], STOPPED,
           T["RunRingDashes"] if armed else 0)


def scope(stopped=False, armed=False):
    """Triggered mode with the trigger level being changed."""
    c = Canvas()
    draw_grid(c)
    # Sub-pixel y + antialiased lines, matching the scope modes.
    for color, amp, freq, phase in ((TRACE_B, 55, 2.0, 0.6),
                                    (TRACE_A, 78, 1.0, 0.0)):
        prev = None
        for x in range(W):
            y = CY - amp * math.sin(2 * math.pi * freq * x / W + phase)
            if prev:
                c.line_aa(prev[0], prev[1], x, y, color)
            prev = (x, y)
    band(c, "trigger", "1000", "mv")
    if stopped or armed:
        run_ring(c, armed)
    return c


def scope_stopped():
    return scope(stopped=True)


def scope_armed():
    return scope(armed=True)


# ------------------------------------------------------------ Waterfall ----
HALF = W // 2


def _intensity(cell, line, cells, partials):
    """Synthetic spectrogram: a few slowly drifting partials."""
    v = 0.0
    for k, (f0, drift, amp) in enumerate(partials):
        f = f0 + drift * math.sin(line / 26.0 + k)
        d = abs(cell / cells - f)
        v = max(v, amp * math.exp(-(d * 26) ** 2))
    return int(255 * min(1.0, v))


def _waterfall(flow):
    c = Canvas()
    a = [(0.10, 0.03, 1.0), (0.21, 0.05, 0.55), (0.42, 0.02, 0.3)]
    b = [(0.16, 0.05, 0.9), (0.33, 0.03, 0.5)]
    if flow == "up":
        # Frequency across X rising outward, newest line at the bottom.
        for y in range(H):
            age = H - 1 - y
            for cell in range(HALF):
                va = _intensity(cell, age, HALF, a)
                vb = _intensity(cell, age, HALF, b)
                c.set(HALF - 1 - cell, y, tuple(k * va // 255 for k in TRACE_A))
                c.set(HALF + cell, y, tuple(k * vb // 255 for k in TRACE_B))
    else:
        # Frequency up Y (lowest at the bottom), newest line at the centre.
        for x in range(HALF):
            age = HALF - 1 - x
            for cell in range(H):
                y = H - 1 - cell
                va = _intensity(cell, age, H, a)
                vb = _intensity(cell, age, H, b)
                c.set(x, y, tuple(k * va // 255 for k in TRACE_A))
                c.set(W - 1 - x, y, tuple(k * vb // 255 for k in TRACE_B))
    c.vline(CX - 1, 0, H, GRID)
    c.vline(CX, 0, H, GRID)
    band(c, "flow", flow, "")
    return c


def waterfall_up():
    return _waterfall("up")


def waterfall_out():
    return _waterfall("out")


# -------------------------------------------------------------- Settings ---
NAME_R = CX - MN["kGutter"] // 2
VALUE_L = CX + MN["kGutter"] // 2

# Mirrors the kItems table in Settings.cpp: (name, value, unit).
ITEMS = [("edge", "rising", ""), ("A4", "440", "Hz"), ("persist", "long", "")]


def settings(sel=2):
    c = Canvas()
    y = CY - ((len(ITEMS) - 1) * MN["kRowDy"] + MN["kRowCapH"]) // 2
    for i, (name, val, unit) in enumerate(ITEMS):
        lit = (i == sel)
        c.text(NAME_R - c.width(name, 20), y, name, TEXT if lit else DIM_DARK, 20)
        c.text_unit(VALUE_L, y, val, unit, HILITE if lit else DIM_DARK, 20, 14)
        y += MN["kRowDy"]

    icon = ICONS["B1"]
    label_w = c.width("back", 20)
    x = CX - (icon[0] + MN["kHintGap"] + label_w) // 2
    c.icon(x, MN["kHintY"], icon, TEXT)
    c.text(x + icon[0] + MN["kHintGap"], MN["kHintTextY"], "back", TEXT, 20)
    return c


# ------------------------------------------------------ AA investigation ---
# Renders the same content aliased and antialiased, zoomed, so the payoff can
# be judged before committing to it in firmware.
AA_CROPS = {
    "ring":  (150, 8, 90, 78),     # where the ring turns hardest
    "trace": (18, 10, 90, 78),   # a sine peak, where the slope is shallow
}


def _aa_subject(aa, subpixel=False):
    """`subpixel` keeps the trace's fractional y instead of rounding it to a
    pixel the way Mapping::sampleToY does today — the thing that decides
    whether antialiasing the polyline is worth anything."""
    c = Canvas()
    line = c.line_aa if aa else c.line
    prev = None
    for x in range(W):
        y = CY - 92 * math.sin(2 * math.pi * 0.9 * x / W + 0.3)
        if not subpixel:
            y = int(y)
        if prev:
            line(prev[0], prev[1], x, y, TRACE_A)
        prev = (x, y)
    (c.ring if aa else c.ring_hard)(T["RunRingR"], T["RunRingW"], STOPPED)
    return c


def aa_off():
    return _aa_subject(False)


def aa_on():
    return _aa_subject(True)


def aa_sub():
    """Antialiased *and* sub-pixel — what it would take to actually pay off."""
    return _aa_subject(True, subpixel=True)


SCREENS = {
    "aa_off": aa_off,
    "aa_sub": aa_sub,
    "aa_on": aa_on,
    "tuner": tuner,
    "scope_band": scope,
    "scope_stopped": scope_stopped,
    "scope_armed": scope_armed,
    "waterfall_up": waterfall_up,
    "waterfall_out": waterfall_out,
    "settings": settings,
}


def main(argv):
    wanted = argv or sorted(SCREENS)
    for name in wanted:
        if name not in SCREENS:
            sys.exit("unknown screen %r (have: %s)" % (name, ", ".join(sorted(SCREENS))))
        if name.startswith("aa_"):     # zoomed crops, for judging edge quality
            canvas = SCREENS[name]()
            for label, crop in AA_CROPS.items():
                path = os.path.join(OUT, "%s_%s.png" % (name, label))
                canvas.save(path, scale=6, crop=crop)
                print(path)
            continue
        path = os.path.join(OUT, name + ".png")
        SCREENS[name]().save(path)
        print(path)


if __name__ == "__main__":
    main(sys.argv[1:])
