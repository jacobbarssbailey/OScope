// screens/EditValueScreen.cpp — Single-setting live editor implementation.

#include "EditValueScreen.h"
#include "../Theme.h"

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

    // Setting name near the top.
    r.text(Theme::SafeInset, 50, _item->name, Theme::Text, 2);

    // Current value, large and highlighted, in the centre band.
    char val[16];
    _item->format(ctx.settings, val, sizeof val);
    r.text(Theme::SafeInset, 104, val, Theme::Highlight, 3);

    // Control hint.
    r.text(42, 212, "ENC ok   B1 cancel", Theme::Dim, 1);
}
