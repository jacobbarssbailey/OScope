// screens/EditValueScreen.cpp — Single-setting live editor implementation.

#include "EditValueScreen.h"
#include "../Theme.h"
#include "../Fonts.h"
#include "../Icons.h"

void EditValueScreen::onEnter(AppContext& ctx) {
    // Snapshot the whole Settings so B1 can restore on cancel.  Only one field
    // changes, but copying the small struct keeps cancel logic trivial.
    _backup = ctx.settings;
}

void EditValueScreen::handleEvent(const InputEvent& e, AppContext& ctx) {
    if (!_item) { ctx.screens.pop(ctx); return; }  // defensive: nothing to edit

    if (e.type == EventType::EncoderTurn) {
        _item->adjust(ctx.settings, e.delta);       // live edit
    } else if (e.type == EventType::ShortPress) {
        switch (e.button) {
            case Btn::Encoder:                       // confirm: persist, pop
                ctx.settings.save();
                ctx.screens.pop(ctx);
                break;
            case Btn::Mode:                          // cancel: restore, pop
                ctx.settings = _backup;
                ctx.screens.pop(ctx);
                break;
            default:
                break;
        }
    }
}

void EditValueScreen::draw(Renderer& r, AppContext& ctx) {
    r.clear();
    if (!_item) return;

    // Setting name near the top, centered.
    r.textCenterX(72, _item->name, Theme::Text, FONT_BODY);

    // Current value, large and in the primary pink, centered on the face —
    // same value + unit setting as the run screen's parameter band.
    char val[16], unit[8];
    _item->format(ctx.settings, val, sizeof val, unit, sizeof unit);
    r.textUnitCenterX(102, val, unit, Theme::Highlight, FONT_LARGE, FONT_BODY);

    // Control hint: B1 = cancel (the encoder press confirms).
    const int16_t labelW = r.textWidth("cancel", FONT_BODY);
    const int16_t x = (int16_t)(Theme::CX - (IconB1.w + 8 + labelW) / 2);
    r.icon(x, 178, IconB1, Theme::Text);
    r.text((int16_t)(x + IconB1.w + 8), 180, "cancel", Theme::Text, FONT_BODY);
}
