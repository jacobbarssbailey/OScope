// Settings.cpp — Settings defaults, persistence, and the descriptor table.

#include "Settings.h"
#include <Arduino.h>   // snprintf on Teensy
#include <EEPROM.h>

void Settings::defaults() {
    trigSource = TrigSource::A;
    trigEdge   = TrigEdge::Rising;
    trigMode   = TrigMode::Auto;
    a4_hz      = 440;
    persist    = 0;
}

// ---- Persistence ----------------------------------------------------------
// A magic + version header guards against reading uninitialised EEPROM (first
// boot) or a stale layout after a settings-format change; either case falls
// back to defaults.  Bump kVersion whenever the stored fields change.

static constexpr int      kEEAddr  = 0;
static constexpr uint16_t kMagic   = 0x05C0;   // "OScope settings"
static constexpr uint8_t  kVersion = 6;   // bumped: removed grid

struct StoredSettings {
    uint16_t   magic;
    uint8_t    version;
    TrigSource trigSource;
    TrigEdge   trigEdge;
    TrigMode   trigMode;
    uint16_t   a4_hz;
    uint8_t    persist;
};

void Settings::load() {
    StoredSettings s;
    EEPROM.get(kEEAddr, s);
    if (s.magic == kMagic && s.version == kVersion) {
        trigSource = s.trigSource;
        trigEdge   = s.trigEdge;
        trigMode   = s.trigMode;
        a4_hz      = s.a4_hz;
        persist    = s.persist;
    } else {
        defaults();
        save();   // initialise EEPROM so subsequent boots read a valid record
    }
}

void Settings::save() const {
    StoredSettings s{kMagic, kVersion, trigSource, trigEdge, trigMode,
                     a4_hz, persist};
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

// A4 tuner reference: ±1 Hz per detent over the usual instrument range.
static void adjA4(Settings& s, int8_t d) {
    int v = (int)s.a4_hz + d;
    if (v < 400) v = 400;
    if (v > 480) v = 480;
    s.a4_hz = (uint16_t)v;
}
static void fmtA4(const Settings& s, char* b, uint8_t n) {
    snprintf(b, n, "%u Hz", s.a4_hz);
}

// Trace persistence: four levels, stepped (not wrapped) by the encoder.
static void adjPersist(Settings& s, int8_t d) {
    int v = (int)s.persist + d;
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    s.persist = (uint8_t)v;
}
static void fmtPersist(const Settings& s, char* b, uint8_t n) {
    static const char* kNames[4] = {"Off", "Short", "Med", "Long"};
    snprintf(b, n, "%s", kNames[s.persist <= 3 ? s.persist : 0]);
}


// ---- Descriptor table -----------------------------------------------------
static const SettingItem kItems[] = {
    { "Trig Src",  adjSource,  fmtSource  },
    { "Trig Edge", adjEdge,    fmtEdge    },
    { "Trig Mode", adjMode,    fmtMode    },
    { "A4 Tune",   adjA4,      fmtA4      },
    { "Persist",   adjPersist, fmtPersist },
};

const SettingItem* settingItems() { return kItems; }
uint8_t            settingCount() { return sizeof(kItems) / sizeof(kItems[0]); }
