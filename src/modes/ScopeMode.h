// modes/ScopeMode.h — Abstract waveform-rendering strategy interface.
//
// ScopeMode is the strategy base class for each acquisition/display mode
// (Triggered, Rolling, XY, …).  RunScreen holds one ScopeMode* per Mode enum
// value and delegates waveform drawing to the active one each frame.
//
// SampleBuffers holds raw ADC counts for both channels from the most recent
// capture.  All modes read from SampleBuffers; only Acquisition writes to it.
//
// No dynamic allocation: ScopeMode objects and SampleBuffers are statically
// allocated by the caller (OScope.ino / RunScreen).
#pragma once

#include "../Renderer.h"
#include "../ScopeState.h"
#include <stdint.h>

// --------------------------------------------------------------------------
// SampleBuffers — captured ADC data, one sweep.
// --------------------------------------------------------------------------
// Raw counts are 10-bit (0..1023) from analogRead with analogReadResolution(10).
//
// Voltage mapping (documented here, used in sampleToY):
//   Hardware input range : ±10 V (Eurorack signal)
//   Input conditioning   : level-shifted + attenuated to 0..3.3 V at the ADC pin
//   Mid-rail (0 V input) : ~1.65 V → ADC midscale ≈ 512 counts  (3.3/2 * 1023/3.3)
//   Full scale (±10 V)   : maps to 0..1023 counts
//   Scale factor         : 10 V / 512 counts  (= 10000 mV / 512 ≈ 19.53 mV/count)
//   Formula:
//     adc_count = sample                         (0..1023)
//     adc_mid   = 512                            (= 0 V input)
//     mV_input  = (adc_count - adc_mid) * 10000 / 512
//   Inverse (mV → ADC count for trigger threshold):
//     adc_count = adc_mid + mV * 512 / 10000
//
// NOTE: The exact mid-rail ADC value depends on hardware calibration.  512 is
// used as a nominal mid-scale value; adjust ADC_MID in Acquisition.cpp if the
// hardware measures differently.

struct SampleBuffers {
    static constexpr uint16_t N = 240;   // samples per sweep = display width in pixels
    uint16_t ch[2][N];                   // raw 0..1023 ADC counts; ch[0]=A, ch[1]=B
    uint16_t count = 0;                  // number of valid samples (≤ N)
};

// --------------------------------------------------------------------------
// ScopeMode — abstract waveform-rendering strategy.
// --------------------------------------------------------------------------
class ScopeMode {
public:
    virtual ~ScopeMode() {}

    // Human-readable mode name (for debug / future UI use).
    virtual const char* name() const = 0;

    // Render the traces into the framebuffer via r.  The grid is drawn by
    // RunScreen beforehand; render draws traces only.
    // Called before RunScreen draws the HUD, which lands on top.
    // May be called more than once per acquired frame (e.g. on a UI redraw),
    // so it must be a pure function of (state, buf) — no accumulation here.
    // Modes MUST NOT call tft.updateScreen() — the main loop owns that.
    virtual void render(Renderer& r, const ScopeState& state,
                        const SampleBuffers& buf) = 0;

    // Called at most once per rendered frame, immediately before render(), and
    // only when a new sweep has been published since the last one — so a
    // UI-triggered redraw never double-counts a frame, and sweeps acquired
    // faster than the display can show them collapse into the freshest.
    //
    // This is where per-frame analysis belongs (Tuner's YIN, Spectrum's and
    // Waterfall's FFT).  It is deliberately tied to the *display* rate, not the
    // acquisition rate: YIN costs 8 ms, and running it per published sweep put
    // the CPU at 99% once the display transfer stopped throttling the loop.
    virtual void onFrame(const SampleBuffers& /*buf*/) {}

    // --- Optional mode-owned encoder parameters ---
    // A mode with its own adjustable parameter (Tuner) returns true here;
    // RunScreen then routes the encoder button (cycle to next param) and the
    // encoder rotation (adjust the selected param) to this mode instead of the
    // shared Timebase/V-div/Trigger set.  Default: the mode has no parameters
    // of its own.
    virtual bool ownsEncoder() const { return false; }
    virtual void encoderPress() {}
    virtual void encoderTurn(int8_t /*delta*/) {}

    // --- Optional mode-owned B2 (Channel) button ---
    // A mode that repurposes the otherwise-unused B2 short press (Waterfall's
    // flow direction) returns true here and handles it in channelPress(), which
    // fills label/value with a readout for RunScreen's parameter band — leave
    // them empty to raise no band.  Default: B2 does nothing.
    virtual bool ownsChannelButton() const { return false; }
    virtual void channelPress(char* label, uint8_t nl, char* value, uint8_t nv) {
        if (nl) label[0] = '\0';
        if (nv) value[0] = '\0';
    }
};
