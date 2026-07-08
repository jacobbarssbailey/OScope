// ScopeState.cpp — Implementation of ScopeState helpers.

#include "ScopeState.h"
#include <EEPROM.h>

// EEPROM layout: Settings owns address 0 (see Settings.cpp); ScopeState uses a
// separate, non-overlapping slot here.  A magic + version header guards against
// uninitialised EEPROM or a stale layout (bump kStateVersion on field changes).
static constexpr int      kEEStateAddr = 32;
static constexpr uint16_t kStateMagic   = 0x05C1;
static constexpr uint8_t  kStateVersion = 3;   // bumped: added colorScheme field

// Only the acquisition setup is persisted — not running / singleArmed.
struct StoredState {
    uint16_t     magic;
    uint8_t      version;
    Mode         mode;
    ChannelSel   channel;
    EncoderParam selected;
    uint16_t     timebase_us_per_div;
    uint16_t     vscale_mv_per_div[2];
    int16_t      trigger_level_mv;
    bool         channelEnabled[2];
    uint8_t      colorScheme;
};

void ScopeState::resetToDefaults() {
    mode                  = Mode::Triggered;
    channel               = ChannelSel::Both;
    selected              = EncoderParam::Timebase;
    running               = true;
    timebase_us_per_div   = 500;
    vscale_mv_per_div[0]  = 3000;
    vscale_mv_per_div[1]  = 3000;
    trigger_level_mv      = 0;
    channelEnabled[0]     = true;
    channelEnabled[1]     = true;
    colorScheme           = 0;
    singleArmed           = false;
}

void ScopeState::load() {
    StoredState s;
    EEPROM.get(kEEStateAddr, s);
    if (s.magic == kStateMagic && s.version == kStateVersion) {
        mode                 = s.mode;
        channel              = s.channel;
        selected             = s.selected;
        timebase_us_per_div  = s.timebase_us_per_div;
        vscale_mv_per_div[0] = s.vscale_mv_per_div[0];
        vscale_mv_per_div[1] = s.vscale_mv_per_div[1];
        trigger_level_mv     = s.trigger_level_mv;
        channelEnabled[0]    = s.channelEnabled[0];
        channelEnabled[1]    = s.channelEnabled[1];
        colorScheme          = s.colorScheme;
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
                  timebase_us_per_div, {vscale_mv_per_div[0], vscale_mv_per_div[1]},
                  trigger_level_mv, {channelEnabled[0], channelEnabled[1]}, colorScheme};
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
