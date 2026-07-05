// Theme.h — Single source of truth for all display colors and layout constants.
//
// All color values are 16-bit RGB565.  All pixel coordinates assume the
// 240×240 GC9A01A round display with rotation 2.  Any file that would
// otherwise hard-code a color hex or a layout pixel number should reference
// a constant here instead.
#pragma once
#include <stdint.h>

namespace Theme {
  // ---- Colors (RGB565) ----
  constexpr uint16_t Background = 0x0000;  // Black
  constexpr uint16_t Grid       = 0x18E3;  // Dark grey-green
  constexpr uint16_t Frame      = 0xFFFF;  // White
  constexpr uint16_t TraceA     = 0x07E0;  // Green (channel A)
  constexpr uint16_t TraceB     = 0x07FF;  // Cyan  (channel B)
  constexpr uint16_t Text       = 0xFFFF;  // White
  constexpr uint16_t Dim        = 0xC618;  // Light grey (secondary labels)
  constexpr uint16_t Highlight  = 0xFFE0;  // Yellow (selected item)

  // ---- Layout ----
  constexpr int16_t W         = 240;  // Display width  in pixels
  constexpr int16_t H         = 240;  // Display height in pixels
  constexpr int16_t CX        = 120;  // Centre X
  constexpr int16_t CY        = 120;  // Centre Y
  constexpr int16_t SafeInset = 30;   // Min margin from edge for readable content

  // ---- HUD text sizes (Adafruit GFX multiplier: 1 = 6×8 px glyphs) ----
  constexpr uint8_t HudTitleSize = 2;  // Mode label
  constexpr uint8_t HudTextSize  = 2;  // Readout rows (enlarged for legibility)

  // ---- RunScreen text-row positions (tuned for HudTextSize = 2) ----
  // Rows use a 32 px pitch: 16 px-tall size-2 glyphs + 16 px gap.  X origins are
  // pulled in slightly so the wider size-2 strings stay within the round face.
  constexpr int16_t RunModeX      = 90;   // Mode label X
  constexpr int16_t RunModeY      = SafeInset; // Mode label Y (top, y=30)
  constexpr int16_t RunSelLabelX  = 30;   // "Sel:" label X
  constexpr int16_t RunSelY       = 62;   // Selected-parameter row Y
  constexpr int16_t RunSelValueX  = 90;   // Selected-parameter value X (clears "Sel:" at size 2)
  constexpr int16_t RunChanX      = 30;   // Channel row X
  constexpr int16_t RunChanY      = 94;   // Channel row Y
  constexpr int16_t RunStopX      = 30;   // Run/stop indicator X
  constexpr int16_t RunStopY      = 126;  // Run/stop indicator Y

  // Row for the live formatted value of the selected parameter.
  // The parameter name is shown in the "Sel:" row (RunSelY); no separate name row needed.
  constexpr int16_t RunParamValX  = 30;   // Formatted value X
  constexpr int16_t RunParamValY  = 162;  // Formatted value Y

  // ---- Oscilloscope plot area ----
  // The round display is 240×240.  The waveform occupies the full 240×240 canvas;
  // the HUD text is drawn on top at z-order above the waveform so it stays readable.
  // Grid divisions are 30 px × 30 px → 8 columns × 8 rows (8 divs each axis).
  constexpr int16_t PlotX         = 0;    // Plot area left edge (pixels)
  constexpr int16_t PlotY         = 0;    // Plot area top edge (pixels)
  constexpr int16_t PlotW         = 240;  // Plot area width (pixels)
  constexpr int16_t PlotH         = 240;  // Plot area height (pixels)
  constexpr int16_t GridDiv       = 30;   // Grid division size (pixels/div)
  // Number of complete grid divisions per axis: 240 / 30 = 8
  constexpr int16_t GridCols      = 8;    // Horizontal divisions
  constexpr int16_t GridRows      = 8;    // Vertical divisions
  // Centre of the plot area in pixels (mid-rail = 0 V input).
  constexpr int16_t PlotCX        = PlotX + PlotW / 2;  // 120
  constexpr int16_t PlotCY        = PlotY + PlotH / 2;  // 120
}
