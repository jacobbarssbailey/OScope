// Fonts.h — UI typeface for the design refresh: Inter Bold Italic.
//
// Packaged into the GC9A01A_t3n / ILI9341_t3 anti-aliased font system at four
// sizes (see font_Inter.{h,cpp}).  Screens reference the semantic aliases below
// rather than raw size names, so the size scale can be retuned in one place.
#pragma once

#include <GC9A01A_t3n.h>   // defines ILI9341_t3_font_t
#include "font_Inter.h"

#define FONT_SMALL  Inter_14_Bold_Italic   // FPS readout, bottom hint lines
#define FONT_BODY   Inter_20_Bold_Italic   // readouts, menu items, indicators
#define FONT_LARGE  Inter_36_Bold_Italic   // mode flash, edit value
#define FONT_HUGE   Inter_42_Bold_Italic   // tuner hero readout
