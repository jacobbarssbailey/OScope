// Settings.h — Persistent user settings + editable-setting descriptors.
//
// Settings holds configuration that lives across the whole session (and, from
// Task 8, across power cycles via EEPROM).  It is distinct from ScopeState:
// ScopeState is the live acquisition state reset by the encoder long-press,
// whereas Settings is user configuration reached through the menu.
//
// The SettingItem table below drives MenuScreen (list) and EditValueScreen
// (single-value editor) generically, mirroring the Parameter pattern used for
// the encoder-adjustable acquisition params.
#pragma once

#include <stdint.h>

enum class TrigEdge : uint8_t { Rising, Falling };

struct Settings {
    // Trigger source is always channel A and the mode is always auto; only the
    // edge is user-selectable.
    TrigEdge   trigEdge   = TrigEdge::Rising;
    uint16_t   a4_hz      = 440;   // Tuner reference: frequency of note A4
    uint8_t    persist    = 3;     // trace persistence: 0=Off,1=Short,2=Med,3=Long

    // Restore all fields to the compile-time defaults above.
    void defaults();

    // Persistence (Teensy emulated EEPROM).  load() applies the stored settings
    // if a valid record is present, otherwise sets defaults and writes them.
    // save() persists the current settings; call it when an edit is confirmed.
    void load();
    void save() const;
};
// NOTE: display brightness (in the original plan) is intentionally omitted —
// the GC9A01A is wired with no backlight-control pin (see Config.h / README
// pin map), so there is nothing to drive.  Add a field here plus a BL pin if
// the hardware later gains a backlight line.

// ---- Editable setting descriptors (used by MenuScreen / EditValueScreen) ----
// Every setting is a small discrete set, so adjust() cycles/toggles the value
// and format() writes its current label.  Same shape as Parameter: the value
// and its unit are separate strings because they are set at different sizes on
// a shared baseline ("440" + "Hz").  A setting with no unit leaves it empty.
struct SettingItem {
    const char* name;
    void (*adjust)(Settings&, int8_t delta);
    void (*format)(const Settings&, char* val, uint8_t nv,
                   char* unit, uint8_t nu);
};

// Access to the static descriptor table.
const SettingItem* settingItems();
uint8_t            settingCount();
