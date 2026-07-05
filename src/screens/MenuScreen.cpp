// screens/MenuScreen.cpp — Settings list screen implementation.

#include "MenuScreen.h"
#include "EditValueScreen.h"
#include "../Settings.h"
#include "../Theme.h"

// Layout constants for the list (size-2 rows, tuned for the round face).
static constexpr int16_t kTitleX = 72;
static constexpr int16_t kTitleY = 18;
static constexpr int16_t kRow0Y  = 54;
static constexpr int16_t kRowDy  = 34;
static constexpr int16_t kNameX  = 20;
static constexpr int16_t kValueX = 145;

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
    r.text(kTitleX, kTitleY, "SETTINGS", Theme::Text, 2);

    const SettingItem* items = settingItems();
    const uint8_t count = settingCount();
    char val[16];
    int16_t y = kRow0Y;
    for (uint8_t i = 0; i < count; ++i) {
        const bool sel = (i == _sel);
        const uint16_t nameColor = sel ? Theme::Highlight : Theme::Text;
        const uint16_t valColor  = sel ? Theme::Highlight : Theme::Dim;
        r.text(kNameX, y, items[i].name, nameColor, 2);
        items[i].format(ctx.settings, val, sizeof val);
        r.text(kValueX, y, val, valColor, 2);
        y += kRowDy;
    }

    r.text(52, 212, "ENC edit   B1 back", Theme::Dim, 1);
}
