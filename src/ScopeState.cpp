// ScopeState.cpp — Implementation of ScopeState helpers.

#include "ScopeState.h"

void ScopeState::resetToDefaults() {
    mode                  = Mode::Triggered;
    channel               = ChannelSel::A;
    selected              = EncoderParam::Timebase;
    running               = true;
    timebase_us_per_div   = 500;
    vscale_mv_per_div[0]  = 700;
    vscale_mv_per_div[1]  = 700;
    trigger_level_mv      = 0;
    channelEnabled[0]     = true;
    channelEnabled[1]     = true;
    singleArmed           = false;
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
