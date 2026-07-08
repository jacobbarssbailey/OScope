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

  // ---- RunScreen HUD positions (v2, anti-aliased Arial fonts) ----
  // The waveform fills the whole canvas; the HUD is minimal and mostly hidden.
  // All these readouts are horizontally centered (textCenterX); the Y here is the
  // top of the text.  Tuned for the round face — kept clear of the edges.
  constexpr int16_t StopY   = 20;   // "STOP"/"ARM" top indicator (Arial 16)
  constexpr int16_t ModeY   = 104;  // mode flash, vertically ~centered (Arial 24)
  constexpr int16_t ParamY  = 196;  // selected-param readout, near bottom (Arial 16)

  // FPS readout (Arial 13), top-left of the safe band — clear of the centered HUD.
  constexpr int16_t FpsX    = 40;
  constexpr int16_t FpsY    = 30;

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
