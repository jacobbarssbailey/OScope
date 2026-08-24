// screens/MenuScreen.cpp — Settings list screen implementation.

#include "MenuScreen.h"
#include "EditValueScreen.h"
#include "../Settings.h"
#include "../Theme.h"
#include "../Fonts.h"
#include "../Icons.h"

// The list is a two-column ledger straddling the display's vertical centre:
// names right-aligned into it, values left-aligned out of it, kGutter apart.
// The block of rows is centred vertically on the face; the only other thing on
// screen is the B1 = back hint at the bottom.
static constexpr int16_t kRowDy   = 32;   // baseline-to-baseline row pitch
static constexpr int16_t kRowCapH = 20;   // FONT_BODY cap height
static constexpr int16_t kGutter  = 12;   // gap between the name and value columns
static constexpr int16_t kNameR   = Theme::CX - kGutter / 2;   // names end here
static constexpr int16_t kValueL  = Theme::CX + kGutter / 2;   // values start here

// Bottom hint: the B1 icon and its label, centred as a unit.
static constexpr int16_t kHintY     = 202;  // icon top (24 px tall)
static constexpr int16_t kHintTextY = 204;  // label top, optically centred on it
static constexpr int16_t kHintGap   = 8;

void MenuScreen::onEnter(AppContext& /*ctx*/) {
    _sel = 0;
}

void MenuScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
    const uint8_t count = settingCount();

    if (e.type == EventType::EncoderTurn) {
        // Move highlight, wrapping at both ends.
        int v = (int)_sel + (e.delta > 0 ? 1 : -1);
        if (v < 0) v = count - 1;
        if (v >= count) v = 0;
        _sel = (uint8_t)v;
    } else if (e.type == EventType::ShortPress) {
        switch (e.button) {
            case Btn::Encoder:  // open the highlighted setting
                if (_edit) {
                    _edit->setItem(&settingItems()[_sel]);
                    ctx.screens.push(_edit, ctx);
                }
                break;
            case Btn::Mode:     // B1: back to the run screen
                ctx.screens.pop(ctx);
                break;
            default:
                break;
        }
    }
}

void MenuScreen::draw(Renderer& r, AppContext& ctx) {
    r.clear();

    const SettingItem* items = settingItems();
    const uint8_t count = settingCount();

    // Centre the block of rows on the face.
    int16_t y = (int16_t)(Theme::CY - ((count - 1) * kRowDy + kRowCapH) / 2);

    char val[16], unit[8];
    for (uint8_t i = 0; i < count; ++i) {
        // Only the highlighted row is lit: its name white, its value in the
        // primary pink.  Everything else recedes to dark grey.
        const bool sel = (i == _sel);
        const uint16_t nameColor = sel ? Theme::Text      : Theme::DimDark;
        const uint16_t valColor  = sel ? Theme::Highlight : Theme::DimDark;

        const int16_t nameW = r.textWidth(items[i].name, FONT_BODY);
        r.text((int16_t)(kNameR - nameW), y, items[i].name, nameColor, FONT_BODY);

        items[i].format(ctx.settings, val, sizeof val, unit, sizeof unit);
        r.textUnit(kValueL, y, val, unit, valColor, FONT_BODY, FONT_SMALL);

        y += kRowDy;
    }

    // B1 = back, as the button's own icon plus its label.
    const int16_t labelW = r.textWidth("back", FONT_BODY);
    const int16_t x = (int16_t)(Theme::CX - (IconB1.w + kHintGap + labelW) / 2);
    r.icon(x, kHintY, IconB1, Theme::Text);
    r.text((int16_t)(x + IconB1.w + kHintGap), kHintTextY, "back", Theme::Text,
           FONT_BODY);
}
