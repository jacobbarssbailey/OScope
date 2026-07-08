// Fonts.h — Anti-aliased t3 fonts for the UI (Arial 13 / 16 / 24).
//
// The three UI sizes chosen on hardware:
//   Arial_13 — small (FPS, hints)
//   Arial_16 — default (readouts, menu items, STOP indicator)
//   Arial_24 — large (mode flash)
//
// font_Arial.h ships with the Teensy ILI9341_t3 library.  It #includes
// "ILI9341_t3.h" only to obtain the ILI9341_t3_font_t struct — but GC9A01A_t3n
// already defines that struct, so we pre-define ILI9341_t3.h's include guard to
// skip it and avoid a duplicate-struct compile error.
#pragma once

#include <GC9A01A_t3n.h>   // defines ILI9341_t3_font_t (via ILI9341_fonts.h)

#ifndef _ILI9341_t3H_
#  define _ILI9341_t3H_
#endif
#include "font_Arial.h"
