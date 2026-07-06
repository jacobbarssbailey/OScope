// Settings.cpp — Settings defaults, persistence, and the descriptor table.

#include "Settings.h"
#include <Arduino.h>   // snprintf on Teensy
#include <EEPROM.h>

void Settings::defaults() {
    trigSource = TrigSource::A;
    trigEdge   = TrigEdge::Rising;
    trigMode   = TrigMode::Auto;
    grid       = true;
}

// ---- Persistence ----------------------------------------------------------
// A magic + version header guards against reading uninitialised EEPROM (first
// boot) or a stale layout after a settings-format change; either case falls
// back to defaults.  Bump kVersion whenever the stored fields change.

static constexpr int      kEEAddr  = 0;
static constexpr uint16_t kMagic   = 0x05C0;   // "OScope settings"
static constexpr uint8_t  kVersion = 1;

struct StoredSettings {
    uint16_t   magic;
    uint8_t    version;
    TrigSource trigSource;
    TrigEdge   trigEdge;
    TrigMode   trigMode;
    bool       grid;
};

void Settings::load() {
    StoredSettings s;
    EEPROM.get(kEEAddr, s);
    if (s.magic == kMagic && s.version == kVersion) {
        trigSource = s.trigSource;
        trigEdge   = s.trigEdge;
        trigMode   = s.trigMode;
        grid       = s.grid;
    } else {
        defaults();
        save();   // initialise EEPROM so subsequent boots read a valid record
    }
}

void Settings::save() const {
    StoredSettings s{kMagic, kVersion, trigSource, trigEdge, trigMode, grid};
    EEPROM.put(kEEAddr, s);   // put() only rewrites changed bytes (flash wear)
}

// ---- Per-setting adjust/format helpers -----------------------------------
// Each setting is a two-state toggle, so any nonzero encoder delta flips it.

static void adjSource(Settings& s, int8_t d) {
    if (d) s.trigSource = (s.trigSource == TrigSource::A) ? TrigSource::B : TrigSource::A;
}
static void fmtSource(const Settings& s, char* b, uint8_t n) {
    snprintf(b, n, "%s", s.trigSource == TrigSource::A ? "A" : "B");
}

static void adjEdge(Settings& s, int8_t d) {
    if (d) s.trigEdge = (s.trigEdge == TrigEdge::Rising) ? TrigEdge::Falling : TrigEdge::Rising;
}
static void fmtEdge(const Settings& s, char* b, uint8_t n) {
    snprintf(b, n, "%s", s.trigEdge == TrigEdge::Rising ? "Rising" : "Falling");
}

static void adjMode(Settings& s, int8_t d) {
    if (d) s.trigMode = (s.trigMode == TrigMode::Auto) ? TrigMode::Normal : TrigMode::Auto;
}
static void fmtMode(const Settings& s, char* b, uint8_t n) {
    snprintf(b, n, "%s", s.trigMode == TrigMode::Auto ? "Auto" : "Normal");
}

static void adjGrid(Settings& s, int8_t d) {
    if (d) s.grid = !s.grid;
}
static void fmtGrid(const Settings& s, char* b, uint8_t n) {
    snprintf(b, n, "%s", s.grid ? "On" : "Off");
}

// ---- Descriptor table -----------------------------------------------------
static const SettingItem kItems[] = {
    { "Trig Src",  adjSource, fmtSource },
    { "Trig Edge", adjEdge,   fmtEdge   },
    { "Trig Mode", adjMode,   fmtMode   },
    { "Grid",      adjGrid,   fmtGrid   },
};

const SettingItem* settingItems() { return kItems; }
uint8_t            settingCount() { return sizeof(kItems) / sizeof(kItems[0]); }
