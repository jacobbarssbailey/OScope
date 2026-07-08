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
  // These were compile-time constants until the color-scheme feature; they are
  // now mutable globals so a palette can be swapped at runtime (see Palette
  // below and applyPalette()).  Call sites are unchanged — they still read
  // Theme::Background etc., now getting whatever the active palette last set.
  // Defaults below reproduce the original "Classic" scheme exactly.
  extern uint16_t Background;  // Black
  extern uint16_t Grid;        // Dark grey-green
  extern uint16_t Frame;       // White
  extern uint16_t TraceA;      // Green (channel A)
  extern uint16_t TraceB;      // Cyan  (channel B)
  extern uint16_t Text;        // White
  extern uint16_t Dim;         // Light grey (secondary labels)
  extern uint16_t Highlight;   // Yellow (selected item)

  // ---- Color schemes ----
  // One immutable palette per selectable scheme.  Field order matches the color
  // globals above.  applyPalette(i) copies palette i into those globals; the
  // rest of the app then reads the new colors on its next draw.
  struct Palette {
    const char* name;
    uint16_t background, grid, frame, traceA, traceB, text, dim, highlight;
  };

  // Number of available schemes, and the display name of scheme i (clamped).
  uint8_t     paletteCount();
  const char* paletteName(uint8_t i);

  // Copy scheme i's colors into the active color globals.  Index is clamped to
  // [0, paletteCount()-1] so an out-of-range stored value can't corrupt state.
  void applyPalette(uint8_t i);

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
  constexpr int16_t FpsX    = 105;
  constexpr int16_t FpsY    = 170;

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
