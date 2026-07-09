// Theme.cpp — Runtime color state and the color-scheme palette table.
//
// The color globals declared in Theme.h live here.  They are initialised to the
// "Classic" scheme (index 0) so the display looks correct before any palette is
// explicitly applied.  applyPalette() overwrites them from kPalettes[].

#include "Theme.h"

namespace Theme {

// Active colors — initialised to Classic (kPalettes[0]).  Mutated by applyPalette().
uint16_t Background = 0x0000;  // Black
uint16_t Grid       = 0x18E3;  // Dark grey-green
uint16_t Frame      = 0xFFFF;  // White
uint16_t TraceA     = 0x07E0;  // Green (channel A)
uint16_t TraceB     = 0x07FF;  // Cyan  (channel B)
uint16_t Text       = 0xFFFF;  // White
uint16_t Dim        = 0xC618;  // Light grey
uint16_t Highlight  = 0xFFE0;  // Yellow

// ---- Palette table -------------------------------------------------------
// Fields: name, background, grid, frame, traceA, traceB, text, dim, highlight.
// All colors are RGB565.  Index 0 MUST be Classic (matches the defaults above
// and the original hard-coded theme).  Non-Classic values were converted from
// #RRGGBB source colors, so they are the nearest RGB565 approximation.
static const Palette kPalettes[] = {
  // 0 — Classic: the original OScope theme (green/cyan on black).
  { "Classic",    0x0000, 0x18E3, 0xFFFF, 0x07E0, 0x07FF, 0xFFFF, 0xC618, 0xFFE0 },
  // 1 — Phosphor: monochrome green like an old CRT scope.
  { "Phosphor",   0x0000, 0x09C1, 0x1AC3, 0x37E6, 0xB7EF, 0x37E6, 0x1BC3, 0xCFEC },
  // 2 — Aqua CRT: the phosphor look shifted toward blue/teal.
  { "Aqua CRT",   0x0042, 0x0988, 0x1ACD, 0x37FA, 0x5E5F, 0x4FFC, 0x1B90, 0xA7FE },
  // 3 — Hazel: the witchhazel.thea.codes palette (purple/mint/pink), with the
  //     background deepened from the original #433E56 for stronger contrast.
  { "Hazel",      0x20E6, 0x398A, 0xC51F, 0xC7FB, 0xFDDA, 0xFFDE, 0xAD39, 0xFFD7 },
  // 4 — Amber: classic amber monochrome terminal.
  { "Amber",      0x0820, 0x3920, 0x7A82, 0xFD80, 0xFECE, 0xFD80, 0x8AE2, 0xFF56 },
  // 5 — Nord: cool arctic blues and muted aurora accents (background darkened
  //     below the standard #2E3440 polar night for stronger contrast).
  { "Nord",       0x2125, 0x29A8, 0x4AAD, 0x8E1A, 0xA5F1, 0xEF7E, 0x7C54, 0xEE51 },
  // 6 — Synthwave: neon cyan/magenta on deep indigo.
  { "Synthwave",  0x1887, 0x28CB, 0xF814, 0x079F, 0xF972, 0xF73F, 0x8B57, 0xFF0C },
  // 7 — Solarized: Ethan Schoonover's Solarized Dark.
  { "Solarized",  0x0146, 0x01A8, 0x5B6E, 0x2D13, 0x84C0, 0x9514, 0x5B6E, 0xB440 },
  // 8 — Rose Pine: soft rose/foam/gold on a warm dark base.
  { "Rose Pine",  0x18A4, 0x2107, 0x6B50, 0x9E7B, 0xEDF7, 0xE6FE, 0x6B50, 0xF60E },
  // 9 — Sepia: the warm "plotter paper" palette inverted to light-on-dark —
  //     cream text and earthy green/terracotta traces on a warm near-black.
  { "Sepia",      0x1081, 0x2923, 0x6AC8, 0x5DEF, 0xE38A, 0xEF3A, 0x8C0D, 0xE508 },
};

static constexpr uint8_t kCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

uint8_t paletteCount() { return kCount; }

const char* paletteName(uint8_t i) {
  if (i >= kCount) i = kCount - 1;
  return kPalettes[i].name;
}

void applyPalette(uint8_t i) {
  if (i >= kCount) i = kCount - 1;
  const Palette& p = kPalettes[i];
  Background = p.background;
  Grid       = p.grid;
  Frame      = p.frame;
  TraceA     = p.traceA;
  TraceB     = p.traceB;
  Text       = p.text;
  Dim        = p.dim;
  Highlight  = p.highlight;
}

}  // namespace Theme
