// screens/MenuScreen.h — Settings list screen.
//
// Presents the SettingItem table (from Settings.h) as a scrollable list showing
// each setting's name and current value.  Encoder turn moves the highlight;
// encoder press opens the highlighted setting in EditValueScreen; B1 (Mode)
// pops back to whatever pushed the menu (the run screen).
#pragma once

#include "Screen.h"

class EditValueScreen;

class MenuScreen : public Screen {
public:
    // Wire the shared editor used when a list item is opened.
    void setEditScreen(EditValueScreen* e) { _edit = e; }

    void onEnter(AppContext& ctx) override;      // reset highlight to top
    void handleEvent(const InputEvent& e, AppContext& ctx) override;
    void draw(Renderer& r, AppContext& ctx) override;

private:
    EditValueScreen* _edit = nullptr;
    uint8_t          _sel  = 0;   // highlighted list index
};
