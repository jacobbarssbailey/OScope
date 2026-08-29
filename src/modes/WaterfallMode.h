// modes/WaterfallMode.h — Scrolling spectrogram (spectrum waterfall).
//
// The time-domain complement to Spectrum: instead of drawing the current
// magnitude spectrum as bars, it draws each FFT frame as a line of colour
// (magnitude → brightness of the channel's trace colour) and scrolls those
// lines over time, so you see how the spectrum evolves.
//
// Dual channel, split left/right: channel A owns the left half of the face and
// channel B the right, mirrored about the vertical centre line.
//
// B2 switches the flow direction, which transposes both axes:
//   Up  — frequency across X (rising outward, toward each half's outer edge),
//         time down Y with the newest line at the bottom, scrolling up.
//   Out — frequency up Y (lowest at the bottom), time across X with the newest
//         line at the centre, scrolling outward toward both edges.
//
// The two directions need the same total history (frequency cells × time lines
// = kCellsUp × kLinesUp = kCellsOut × kLinesOut), so one buffer per channel
// serves both; switching direction re-maps it and clears the history.
//
// Free-running like Spectrum: it reads its own 256-sample block from the rings
// via Acquisition::readNewestBlock() and shares Spectrum's fixed ~32 kHz rate.
// The FFT + a new history line are produced in onFrame(); render() paints the
// whole history each frame through a precomputed colour LUT.
#pragma once

#include "ScopeMode.h"
#include <arm_math.h>

class Acquisition;

class WaterfallMode : public ScopeMode {
public:
    WaterfallMode();

    const char* name() const override { return "WFAL"; }

    void setSource(Acquisition* acq) { _src = acq; }

    void render(Renderer& r, const ScopeState& state,
                const SampleBuffers& buf) override;
    void onFrame(const SampleBuffers& buf) override;

    // B2 toggles the flow direction (see the header comment).
    bool ownsChannelButton() const override { return true; }
    void channelPress() override;

private:
    enum class Flow : uint8_t { Up, Out };

    static constexpr uint16_t kFFT  = 256;          // FFT length
    static constexpr uint16_t kNBin = kFFT / 2;     // usable bins (1..128)
    static constexpr uint16_t kW    = 240;          // screen width
    static constexpr uint16_t kH    = 240;          // screen height
    static constexpr uint16_t kHalf = kW / 2;       // width of one channel's half

    // Flow::Up  — frequency across a half (120 cells), time down the face (240).
    // Flow::Out — frequency up the face (240 cells), time across a half (120).
    static constexpr uint16_t kCellsUp   = kHalf;
    static constexpr uint16_t kLinesUp   = kH;
    static constexpr uint16_t kCellsOut  = kH;
    static constexpr uint16_t kLinesOut  = kHalf;
    static constexpr uint16_t kMaxCells  = kCellsOut;
    static constexpr uint16_t kHistBytes = kCellsUp * kLinesUp;   // == kCellsOut*kLinesOut

    static constexpr int32_t kFminHz = 0;           // frequency window (matches Spectrum)
    static constexpr int32_t kFmaxHz = 8192;
    static constexpr float   kFull    = 16384.0f;   // magnitude at full intensity
    static constexpr float   kFloorDb = -48.0f;     // dB at zero intensity

    Acquisition* _src = nullptr;
    arm_rfft_fast_instance_f32 _fft;
    float    _win[kFFT];
    float    _in[kFFT];
    float    _out[kFFT];
    uint16_t _rawA[kFFT];
    uint16_t _rawB[kFFT];
    float    _mag[kNBin];       // per-bin magnitudes (reused per channel)

    Flow     _flow  = Flow::Up;
    uint16_t _cells = kCellsUp; // frequency cells per line, for the current flow
    uint16_t _lines = kLinesUp; // time lines held, for the current flow
    uint16_t _head  = 0;        // ring index of the newest line

    // Per-channel intensity history (0..255): _lines lines of _cells bytes,
    // used as a ring so a new line costs one write pass instead of a scroll.
    uint8_t  _hist[2][kHistBytes];

    // Intensity -> RGB565: a linear ramp from the background to each channel's
    // trace colour, matching the oscilloscope traces.
    uint16_t _lutA[256];
    uint16_t _lutB[256];
    uint8_t  _cell2bin[kMaxCells];   // frequency cell -> FFT bin index (0..kNBin-1)

    void computeMag(const uint16_t* src, float* mag);
    uint8_t magToIntensity(float mag) const;
    // Rebuild the cell→bin table and clear the history for the current flow.
    void reshape();
    // Write the newest line for one channel into the ring at _head.
    void pushLine(const float* mag, uint8_t* hist);
    // Paint one flow direction into the framebuffer.
    void renderUp(uint16_t* fb) const;
    void renderOut(uint16_t* fb) const;
};
