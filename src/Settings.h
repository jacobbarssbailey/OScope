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

enum class TrigSource : uint8_t { A, B };
enum class TrigEdge   : uint8_t { Rising, Falling };
enum class TrigMode   : uint8_t { Auto, Normal };

struct Settings {
    TrigSource trigSource = TrigSource::A;
    TrigEdge   trigEdge   = TrigEdge::Rising;
    TrigMode   trigMode   = TrigMode::Auto;
    bool       grid       = true;

    // Restore all fields to the compile-time defaults above.
    void defaults();
};
// NOTE: display brightness (in the original plan) is intentionally omitted —
// the GC9A01A is wired with no backlight-control pin (see Config.h / README
// pin map), so there is nothing to drive.  Add a field here plus a BL pin if
// the hardware later gains a backlight line.

// ---- Editable setting descriptors (used by MenuScreen / EditValueScreen) ----
// Every setting is a small discrete set, so adjust() cycles/toggles the value
// and format() writes its current label.  Same shape as Parameter.
struct SettingItem {
    const char* name;
    void (*adjust)(Settings&, int8_t delta);
    void (*format)(const Settings&, char* buf, uint8_t n);
};

// Access to the static descriptor table.
const SettingItem* settingItems();
uint8_t            settingCount();
