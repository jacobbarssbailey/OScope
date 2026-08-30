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
  constexpr uint16_t TraceA     = 0xF81D;  // #FF03EA magenta (channel A)
  constexpr uint16_t TraceB     = 0x5B7D;  // #5B6CED periwinkle (channel B)
  constexpr uint16_t Text       = 0xFFFF;  // White
  constexpr uint16_t Dim        = 0x8C71;  // #8E8E8E light grey (secondary labels)
  constexpr uint16_t DimDark    = 0x4208;  // #404040 dark grey (inactive / behind traces)
  constexpr uint16_t Highlight  = 0xF81D;  // Primary pink (= TraceA), selected value
  // #FF6900 orange — the run-state ring.  Warm enough to read as "halted" while
  // sitting ~80 deg of hue away from the pink trace; a true red would be only
  // ~55 deg off and vibrate against it.  Quantises to RGB565 exactly.
  constexpr uint16_t Stopped    = 0xFB40;

  // ---- Layout ----
  constexpr int16_t W         = 240;  // Display width  in pixels
  constexpr int16_t H         = 240;  // Display height in pixels
  constexpr int16_t CX        = 120;  // Centre X
  constexpr int16_t CY        = 120;  // Centre Y
  constexpr int16_t SafeInset = 30;   // Min margin from edge for readable content

  // ---- Run-state ring ----
  // Halted state is shown as a border around the bezel rather than a word in
  // the middle of the trace: solid when frozen, dashed while a single-shot is
  // armed.  Inset one pixel so a hair of physical bezel cannot swallow it.
  constexpr int16_t RunRingR     = 119;  // outer radius
  constexpr int16_t RunRingW     = 2;    // thickness
  constexpr uint8_t RunRingDashes = 12;  // dashes around the ring when armed

  // ---- Transient settings overlay ----
  // Acquisition settings are not on screen permanently: touching one dims the
  // whole face and lists every parameter that applies in the current mode, one
  // icon + value row each, with the edited row in Text and the rest in Dim.
  //
  // This is the only transient readout.  A control the encoder does not walk
  // through, and whose effect is visible on the face (Waterfall's flow
  // direction), reports nothing — it shows itself.
  //
  // OverlayKeep is the per-channel multiplier the dim applies (out of 256): the
  // design's 60% black scrim leaves 40% of the frame, and 102/256 = 0.398.
  constexpr uint16_t OverlayKeep    = 102;
  constexpr uint32_t SettingsHoldMs = 2000; // time on screen after the last input
  constexpr int16_t  SettingIconX   = 26;   // left edge of the icon (32-34 px wide)
  constexpr int16_t  SettingValueX  = 68;   // left edge of the value text
  constexpr int16_t  SettingRowH    = 50;   // row pitch
  constexpr int16_t  SettingRowsCY  = 120;  // the row block is centred on this
  constexpr int16_t  SettingIconSz  = 32;   // icon height, for centring in a row
  // Cap height of the value font (FONT_BODY), for centring text against the icon.
  constexpr int16_t  SettingValueCap = 20;

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

  // ---- Spectrum mode layout ----
  // 128 FFT buckets, each SpecBucketW px wide, centred horizontally.  Channel A
  // grows up from the centre line, channel B grows down (inverted).  At 1 px per
  // bucket the 128 px block clears the round bezel even at full height, so no
  // bars are clipped.
  constexpr int16_t SpecBuckets   = 128;
  constexpr int16_t SpecBucketW   = 1;                              // px per bucket
  constexpr int16_t SpecBarsW     = SpecBuckets * SpecBucketW;      // 128
  constexpr int16_t SpecLeftX     = PlotX + (PlotW - SpecBarsW) / 2; // 56
  constexpr int16_t SpecCenterY   = PlotCY;                         // 120
  constexpr int16_t SpecMaxPx     = 80;                             // full-scale bar height
}
