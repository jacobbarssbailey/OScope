// screens/EditValueScreen.h — Single-setting live editor.
//
// Shows one SettingItem's name and current value.  Encoder turns adjust the
// value live; encoder press confirms (pop); B1 (Mode) cancels and restores the
// value the setting held on entry.  MenuScreen selects which item to edit via
// setItem() before pushing this screen.
#pragma once

#include "Screen.h"
#include "../Settings.h"

class EditValueScreen : public Screen {
public:
    // Choose which setting to edit; call before pushing this screen.
    void setItem(const SettingItem* item) { _item = item; }

    void onEnter(AppContext& ctx) override;      // snapshot for cancel
    void handleEvent(const InputEvent& e, AppContext& ctx) override;
    void draw(Renderer& r, AppContext& ctx) override;

private:
    const SettingItem* _item = nullptr;
    Settings           _backup;   // full snapshot restored on cancel
};
