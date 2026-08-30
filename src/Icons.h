// Icons.h — Small monochrome UI glyphs (control-panel button indicators).
//
// Each icon is an 8-bit coverage mask: one byte per pixel, row-major, where the
// value is the icon's own greyscale composited on black (255 = the white ring,
// 142 = the #8E8E8E body, 0 = transparent).  Renderer::icon() scales a tint
// colour by that byte, so drawing an icon in white reproduces the source art
// exactly and any other tint recolours it uniformly.
//
// One set mirrors the physical controls: B1/B2/B3 are the three buttons (one
// ring gap per button number) and Enc is the rotary encoder; Left/Right are the
// turn-direction arrows.  The other names the encoder parameters, for the
// settings overlay: Timebase, VScale, Trigger.
//
// The design ships each parameter icon twice, white and #8E8E8E, for the edited
// and un-edited states.  Only the white master is stored: the grey variant is
// exactly the same art tinted with Theme::Dim, which is what icon() does.
//
// Generated from tools/icons/art/ by tools/icons/gen_icons.py.
#pragma once

#include <stdint.h>

struct Icon {
    uint8_t        w;
    uint8_t        h;
    const uint8_t* gray;   // w*h coverage bytes, row-major
};

extern const Icon IconB1;
extern const Icon IconB2;
extern const Icon IconB3;
extern const Icon IconEnc;
extern const Icon IconLeft;
extern const Icon IconRight;

extern const Icon IconTimebase;
extern const Icon IconVScale;
extern const Icon IconTrigger;
