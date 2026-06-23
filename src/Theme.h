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

  // ---- RunScreen text-row positions ----
  constexpr int16_t RunModeX      = 90;   // Mode label X (centres text in safe band)
  constexpr int16_t RunModeY      = SafeInset; // Mode label Y (top safe-band row)
  constexpr int16_t RunSelLabelX  = 40;   // "Sel:" label X
  constexpr int16_t RunSelY       = 90;   // Selected-parameter row Y
  constexpr int16_t RunSelValueX  = 80;   // Selected-parameter value X
  constexpr int16_t RunChanX      = 40;   // Channel row X
  constexpr int16_t RunChanY      = 110;  // Channel row Y
  constexpr int16_t RunStopX      = 40;   // Run/stop indicator X
  constexpr int16_t RunStopY      = 130;  // Run/stop indicator Y

  // Row for the live formatted value of the selected parameter.
  // The parameter name is shown in the "Sel:" row (RunSelY); no separate name row needed.
  constexpr int16_t RunParamValX  = 40;   // Formatted value X
  constexpr int16_t RunParamValY  = 170;  // Formatted value Y
}
