// Settings.cpp — Settings defaults and the editable-setting descriptor table.

#include "Settings.h"
#include <Arduino.h>   // snprintf on Teensy

void Settings::defaults() {
    trigSource = TrigSource::A;
    trigEdge   = TrigEdge::Rising;
    trigMode   = TrigMode::Auto;
    grid       = true;
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
