// screens/EditValueScreen.cpp — Single-setting live editor implementation.

#include "EditValueScreen.h"
#include "../Theme.h"
#include "../Fonts.h"

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
                // The Color setting previews live by mutating the active palette;
                // restoring the backup value alone won't undo that, so re-apply
                // the restored scheme.  Harmless (a no-op recolor) for other
                // settings, which never touch the palette.
                Theme::applyPalette(ctx.settings.colorScheme);
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
    r.textCenterX(48, _item->name, Theme::Text, Arial_16);

    // Current value, large and highlighted, centered in the middle band.
    char val[16];
    _item->format(ctx.settings, val, sizeof val);
    r.textCenterX(104, val, Theme::Highlight, Arial_24);

    // Control hint.
    r.textCenterX(180, "ENC ok   B1 cancel", Theme::Dim, Arial_13);
}
