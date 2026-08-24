// Settings.cpp — Settings defaults, persistence, and the descriptor table.

#include "Settings.h"
#include <Arduino.h>   // snprintf on Teensy
#include <EEPROM.h>

void Settings::defaults() {
    trigEdge   = TrigEdge::Rising;
    a4_hz      = 440;
    persist    = 0;
}

// ---- Persistence ----------------------------------------------------------
// A magic + version header guards against reading uninitialised EEPROM (first
// boot) or a stale layout after a settings-format change; either case falls
// back to defaults.  Bump kVersion whenever the stored fields change.

static constexpr int      kEEAddr  = 0;
static constexpr uint16_t kMagic   = 0x05C0;   // "OScope settings"
static constexpr uint8_t  kVersion = 7;   // bumped: removed trigSource + trigMode

struct StoredSettings {
    uint16_t   magic;
    uint8_t    version;
    TrigEdge   trigEdge;
    uint16_t   a4_hz;
    uint8_t    persist;
};

void Settings::load() {
    StoredSettings s;
    EEPROM.get(kEEAddr, s);
    if (s.magic == kMagic && s.version == kVersion) {
        trigEdge   = s.trigEdge;
        a4_hz      = s.a4_hz;
        persist    = s.persist;
    } else {
        defaults();
        save();   // initialise EEPROM so subsequent boots read a valid record
    }
}

void Settings::save() const {
    StoredSettings s{kMagic, kVersion, trigEdge, a4_hz, persist};
    EEPROM.put(kEEAddr, s);   // put() only rewrites changed bytes (flash wear)
}

// ---- Per-setting adjust/format helpers -----------------------------------

// Trigger edge: a two-state toggle, so any nonzero encoder delta flips it.
static void adjEdge(Settings& s, int8_t d) {
    if (d) s.trigEdge = (s.trigEdge == TrigEdge::Rising) ? TrigEdge::Falling : TrigEdge::Rising;
}
static void fmtEdge(const Settings& s, char* b, uint8_t n, char* unit, uint8_t nu) {
    snprintf(b, n, "%s", s.trigEdge == TrigEdge::Rising ? "rising" : "falling");
    if (nu) unit[0] = '\0';
}

// A4 tuner reference: ±1 Hz per detent over the usual instrument range.
static void adjA4(Settings& s, int8_t d) {
    int v = (int)s.a4_hz + d;
    if (v < 400) v = 400;
    if (v > 480) v = 480;
    s.a4_hz = (uint16_t)v;
}
static void fmtA4(const Settings& s, char* b, uint8_t n, char* unit, uint8_t nu) {
    snprintf(b, n, "%u", s.a4_hz);
    snprintf(unit, nu, "Hz");
}

// Trace persistence: four levels, stepped (not wrapped) by the encoder.
static void adjPersist(Settings& s, int8_t d) {
    int v = (int)s.persist + d;
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    s.persist = (uint8_t)v;
}
static void fmtPersist(const Settings& s, char* b, uint8_t n, char* unit, uint8_t nu) {
    static const char* kNames[4] = {"off", "short", "med", "long"};
    snprintf(b, n, "%s", kNames[s.persist <= 3 ? s.persist : 0]);
    if (nu) unit[0] = '\0';
}


// ---- Descriptor table -----------------------------------------------------
// Names are kept short: the menu sets them right-aligned against the values, so
// a long name crowds the value rather than wrapping.
static const SettingItem kItems[] = {
    { "edge",    adjEdge,    fmtEdge    },
    { "A4",      adjA4,      fmtA4      },
    { "persist", adjPersist, fmtPersist },
};

const SettingItem* settingItems() { return kItems; }
uint8_t            settingCount() { return sizeof(kItems) / sizeof(kItems[0]); }
