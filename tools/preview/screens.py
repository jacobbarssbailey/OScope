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
from sim import (Canvas, ICONS, REPO, GRID, TRACE_A, TRACE_B, TEXT, DIM,   # noqa: E402
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


# ------------------------------------------------------ Scope + settings ----
def draw_grid(c):
    for col in range(1, 8):
        c.vline(col * T["GridDiv"], 0, H, GRID)
    for row in range(1, 8):
        c.hline(0, row * T["GridDiv"], W, GRID)


def settings_overlay(c, rows, selected):
    """RunScreen's transient settings overlay: scrim + one row per parameter.

    `rows` is [(icon name, value, unit), ...] and `selected` indexes the row
    being edited — it inks in TEXT, the rest in DIM.
    """
    c.fade(T["OverlayKeep"])
    cy = T["SettingRowsCY"] - (len(rows) - 1) * T["SettingRowH"] // 2
    for i, (icon, value, unit) in enumerate(rows):
        tint = TEXT if i == selected else DIM
        c.icon(T["SettingIconX"], cy - T["SettingIconSz"] // 2, ICONS[icon], tint)
        c.text_unit(T["SettingValueX"], cy - T["SettingValueCap"] // 2,
                    value, unit, tint, 20, 14)
        cy += T["SettingRowH"]


STOPPED = (0xFF, 0x69, 0x00)


def run_ring(c, armed=False):
    """RunScreen's run-state ring: solid when frozen, dashed while armed."""
    c.ring(T["RunRingR"], T["RunRingW"], STOPPED,
           T["RunRingDashes"] if armed else 0)


def draw_traces(c):
    """Two synthetic sweeps, sub-pixel y + antialiased, as the scope modes draw."""
    for color, amp, freq, phase in ((TRACE_B, 55, 2.0, 0.6),
                                    (TRACE_A, 78, 1.0, 0.0)):
        prev = None
        for x in range(W):
            y = CY - amp * math.sin(2 * math.pi * freq * x / W + phase)
            if prev:
                c.line_aa(prev[0], prev[1], x, y, color)
            prev = (x, y)


def scope(stopped=False, armed=False, rows=None, selected=0):
    """Triggered mode, mid-edit: the settings overlay over a live trace."""
    c = Canvas()
    draw_grid(c)
    draw_traces(c)
    if rows is None:
        rows = [("Timebase", "10", "ms"),
                ("VScale", "1", "V"),
                ("Trigger", "2.4", "V")]
    settings_overlay(c, rows, selected)
    if stopped or armed:
        run_ring(c, armed)
    return c


def scope_stopped():
    return scope(stopped=True)


def scope_armed():
    return scope(armed=True)


def scope_settings_wide():
    """Widest the rows get in Triggered: the trigger just under the V crossover."""
    return scope(rows=[("Timebase", "1.5", "ms"),
                       ("VScale", "500", "mV"),
                       ("Trigger", "-980", "mV")], selected=2)


def scope_settings_roll():
    """Rolling drops the trigger row, so two rows re-centre on the face."""
    c = Canvas()
    draw_grid(c)
    draw_traces(c)
    settings_overlay(c, [("Timebase", "500", "ms"), ("VScale", "500", "mV")], 1)
    return c


def scope_clean(stopped=False, armed=False):
    """The same frame with the overlay timed out — what you see most of the time."""
    c = Canvas()
    draw_grid(c)
    draw_traces(c)
    if stopped or armed:
        run_ring(c, armed)
    return c


def scope_frozen():
    """Run/Stop pressed: the last frame held behind a solid ring."""
    return scope_clean(stopped=True)


def scope_single():
    """Run/Stop held: single shot armed, ring dashed until a trigger lands."""
    return scope_clean(armed=True)


# -------------------------------------------------------------- Spectrum ---
# SpecLeftX and SpecCenterY are expressions in Theme.h, so consts() skips them;
# recompute here from the same formulas.
SPEC_LEFT_X   = (W - T["SpecBarsW"]) // 2
SPEC_CENTER_Y = CY


def _spec_mag(f_hz):
    """Synthetic spectrum: a harmonic series over a sloping noise floor.

    Peaks are given a realistic width (a Hann-windowed bin is a few bins wide)
    so the coarse bucket counts have something real to average.
    """
    v = 0.30 * math.exp(-f_hz / 5000.0) + 0.04      # sloping floor
    f0 = 512.0
    for k in range(1, 13):
        amp = 0.95 / (k ** 0.75)
        v = max(v, amp * math.exp(-((f_hz - k * f0) / 190.0) ** 2))
    return min(v, 1.0)


BIN_HZ = 126.0        # SpectrumMode's FFT bin width
SPEC_MAX_HZ = 8192    # SpectrumMode::kMaxHz
N_BIN = 128           # SpectrumMode::kNBin


def _bucket_mag(i, nbins):
    """One bucket's magnitude, mirroring SpectrumMode::mapBars.

    Averages the FFT bin centres that fall inside the bucket, and falls back to
    the nearest bin where the bucket is narrower than the bin spacing.
    """
    f_lo = SPEC_MAX_HZ * i / nbins
    f_hi = SPEC_MAX_HZ * (i + 1) / nbins
    b_lo = max(1, math.ceil(f_lo / BIN_HZ))
    b_hi = min(N_BIN, math.floor(f_hi / BIN_HZ))
    if b_hi >= b_lo:
        return sum(_spec_mag(b * BIN_HZ) for b in range(b_lo, b_hi + 1)) / (b_hi - b_lo + 1)
    b = min(N_BIN, max(1, round((f_lo + f_hi) * 0.5 / BIN_HZ)))
    return _spec_mag(b * BIN_HZ)


SPEC_RAD_INNER = T["SpecRadInner"]
SPEC_RAD_OUTER = T["SpecRadOuter"]


def _spec_radial(nbins, outward):
    """SpectrumMode's radial layouts: each channel's buckets become a fan of
    spokes over a half circle, A on the left and B on the right, low frequency
    at the top of both."""
    c = Canvas()
    span = SPEC_RAD_OUTER - SPEC_RAD_INNER
    for pct in (T["SpecRadRing1"], T["SpecRadRing2"]):
        c.ring(SPEC_RAD_INNER + span * pct // 100, 1, GRID)
    c.ring(SPEC_RAD_OUTER, 1, GRID)
    c.vline(CX, 0, H, GRID)

    wedge = math.pi / nbins
    for side, color, scale in ((-1, TRACE_A, 1.0), (1, TRACE_B, 0.72)):
        for i in range(nbins):
            mag = int(_bucket_mag(i, nbins) * scale * T["SpecMaxPx"])
            length = mag * span // T["SpecMaxPx"]
            if length <= 0:
                continue
            r0 = SPEC_RAD_INNER if outward else SPEC_RAD_OUTER - length
            r1 = SPEC_RAD_INNER + length if outward else SPEC_RAD_OUTER
            # Spoke count follows the arc at this bar's own outer radius.
            spokes = max(1, int(wedge * r1 * 0.9))
            for k in range(spokes):
                t = (i + (k + 0.5) / spokes) * wedge
                sx, sy = side * math.sin(t), -math.cos(t)
                c.line_aa(CX + sx * r0, CY + sy * r0,
                          CX + sx * r1, CY + sy * r1, color)
    return c


def spectrum_radial_out_32():
    return _spec_radial(32, True)


def spectrum_radial_out_128():
    return _spec_radial(128, True)


def spectrum_radial_in_32():
    return _spec_radial(32, False)


def spectrum_radial_in_128():
    return _spec_radial(128, False)


def _spectrum(nbins, gap=0):
    """SpectrumMode at one bucket count: the block keeps its total width as the
    bars widen, and only the outer end of each bar is capped.

    `gap` leaves that many pixels between bars (not in the firmware — it is here
    so the choice can be looked at before committing to it).
    """
    c = Canvas()
    for col in range(1, 8):
        c.vline(col * T["GridDiv"], 0, H, GRID)
    c.hline(0, SPEC_CENTER_Y, W, GRID)

    pitch = T["SpecBarsW"] // nbins
    w = max(1, pitch - gap)
    rad = w // 2
    for i in range(nbins):
        m = _bucket_mag(i, nbins)
        ha = int(m * T["SpecMaxPx"])
        hb = int(m * 0.72 * T["SpecMaxPx"])
        x = SPEC_LEFT_X + i * pitch
        if ha > 0:
            c.bar_rounded(x, SPEC_CENTER_Y - ha, w, ha, rad, TRACE_A, "top")
        if hb > 0:
            c.bar_rounded(x, SPEC_CENTER_Y + 1, w, hb, rad, TRACE_B, "bottom")
    return c


def spectrum_128():
    """The finest setting: 128 buckets at 1 px, unchanged from before."""
    return _spectrum(128)


def spectrum_64():
    return _spectrum(64)


def spectrum_32():
    """The coarsest: 32 buckets at 4 px."""
    return _spectrum(32)


def spectrum_32_gap():
    """32 buckets with a 1 px gap — not in the firmware, drawn for comparison."""
    return _spectrum(32, gap=1)


def spectrum_64_gap():
    return _spectrum(64, gap=1)


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
    "scope_settings": scope,
    "scope_settings_wide": scope_settings_wide,
    "scope_settings_roll": scope_settings_roll,
    "scope_clean": scope_clean,
    "spectrum_128": spectrum_128,
    "spectrum_64": spectrum_64,
    "spectrum_32": spectrum_32,
    "spectrum_32_gap": spectrum_32_gap,
    "spectrum_radial_out_32": spectrum_radial_out_32,
    "spectrum_radial_out_128": spectrum_radial_out_128,
    "spectrum_radial_in_32": spectrum_radial_in_32,
    "spectrum_radial_in_128": spectrum_radial_in_128,
    "spectrum_64_gap": spectrum_64_gap,
    "scope_frozen": scope_frozen,
    "scope_single": scope_single,
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
