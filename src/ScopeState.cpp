// ScopeState.cpp — Implementation of ScopeState helpers.

#include "ScopeState.h"
#include <EEPROM.h>

// EEPROM layout: Settings owns address 0 (see Settings.cpp); ScopeState uses a
// separate, non-overlapping slot here.  A magic + version header guards against
// uninitialised EEPROM or a stale layout (bump kStateVersion on field changes).
static constexpr int      kEEStateAddr = 32;
static constexpr uint16_t kStateMagic   = 0x05C1;
static constexpr uint8_t  kStateVersion = 3;   // bumped: per-mode uint32 timebase

// Only the acquisition setup is persisted — not running / singleArmed.
struct StoredState {
    uint16_t     magic;
    uint8_t      version;
    Mode         mode;
    ChannelSel   channel;
    EncoderParam selected;
    uint32_t     timebase_us_per_div[(uint8_t)Mode::COUNT];
    uint16_t     vscale_mv_per_div[2];
    int16_t      trigger_level_mv;
    bool         channelEnabled[2];
};

void ScopeState::resetToDefaults() {
    mode                  = Mode::Triggered;
    channel               = ChannelSel::Both;
    selected              = EncoderParam::Timebase;
    running               = true;
    // Each mode starts at 500 µs/div — the shared low end of every mode's range.
    for (uint8_t m = 0; m < (uint8_t)Mode::COUNT; ++m)
        timebase_us_per_div[m] = 500;
    vscale_mv_per_div[0]  = 3000;
    vscale_mv_per_div[1]  = 3000;
    trigger_level_mv      = 0;
    channelEnabled[0]     = true;
    channelEnabled[1]     = true;
    singleArmed           = false;
}

void ScopeState::load() {
    StoredState s;
    EEPROM.get(kEEStateAddr, s);
    if (s.magic == kStateMagic && s.version == kStateVersion) {
        mode                 = s.mode;
        channel              = s.channel;
        selected             = s.selected;
        for (uint8_t m = 0; m < (uint8_t)Mode::COUNT; ++m)
            timebase_us_per_div[m] = s.timebase_us_per_div[m];
        vscale_mv_per_div[0] = s.vscale_mv_per_div[0];
        vscale_mv_per_div[1] = s.vscale_mv_per_div[1];
        trigger_level_mv     = s.trigger_level_mv;
        channelEnabled[0]    = s.channelEnabled[0];
        channelEnabled[1]    = s.channelEnabled[1];
    } else {
        resetToDefaults();
        save();   // initialise EEPROM so subsequent boots read a valid record
    }
    // Transient fields always boot to a sane state, regardless of what was saved.
    running     = true;
    singleArmed = false;
}

void ScopeState::save() const {
    StoredState s{kStateMagic, kStateVersion, mode, channel, selected,
                  {timebase_us_per_div[0], timebase_us_per_div[1], timebase_us_per_div[2]},
                  {vscale_mv_per_div[0], vscale_mv_per_div[1]},
                  trigger_level_mv, {channelEnabled[0], channelEnabled[1]}};
    static_assert((uint8_t)Mode::COUNT == 3, "StoredState timebase initializer lists 3 modes");
    EEPROM.put(kEEStateAddr, s);   // put() only rewrites changed bytes (flash wear)
}

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Triggered: return "TRIG";
        case Mode::Rolling:   return "ROLL";
        case Mode::XY:        return "X-Y";
        default:              return "?";
    }
}

const char* channelName(ChannelSel c) {
    switch (c) {
        case ChannelSel::A:    return "A";
        case ChannelSel::B:    return "B";
        case ChannelSel::Both: return "A+B";
        default:               return "?";
    }
}
