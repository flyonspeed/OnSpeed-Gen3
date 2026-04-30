// proto/DisplaySerial.h — OnSpeed `#1` display-serial protocol.
//
// This is the single source of truth for the wire format of the 76-byte ASCII
// frame exchanged between the Gen3 main firmware (producer) and the M5Stack
// secondary display firmware (consumer). (v4.22 size; was 74 bytes in v4.21.)
//
// Frame format (76 bytes total, v4.22):
//
//   Offset  Width  Field               Format   Scale   Notes
//   ------  -----  ------------------  -------  ------  ----------------------
//    0       2     magic               literal  —       "#1"
//    2       4     pitchDeg            %+04d    ×10     signed, –999 to +999
//    6       5     rollDeg             %+05d    ×10     signed, –9999 to +9999
//   11       4     iasKt               %04u     ×10     unsigned, 0–9999
//   15       6     paltFt              %+06d    ×1      signed, –99999 to +99999
//   21       5     turnRateDps         %+05d    ×10     signed, –9999 to +9999
//   26       3     lateralG            %+03d    ×100    signed, –99 to +99
//   29       3     verticalG           %+03d    ×10     signed, –99 to +99
//   32       2     percentLift         %02u     ×1      unsigned, 0–99
//   34       4     vsiFpm10            %+04d    ×1      vsi_fpm/10, –999 to +999
//   38       3     oatC                %+03d    ×1      signed, –99 to +99
//   41       4     flightPathDeg       %+04d    ×10     signed, –999 to +999
//   45       3     flapsDeg            %+03d    ×1      signed, –99 to +99
//   48       2     tonesOnPctLift      %02u     ×1      unsigned, 0–99 (active-detent L/Dmax — operational audio gate, snapped per detent)
//   50       2     onSpeedFastPctLift  %02u     ×1      unsigned, 0–99 (OnSpeedFast body angle through the percent-lift formula)
//   52       2     onSpeedSlowPctLift  %02u     ×1      unsigned, 0–99 (OnSpeedSlow body angle through the percent-lift formula)
//   54       2     stallWarnPctLift    %02u     ×1      unsigned, 0–99 (StallWarn body angle through the percent-lift formula)
//   56       3     flapsMinDeg         %+03d    ×1      signed, –99 to +99 (min configured flap deg)
//   59       3     flapsMaxDeg         %+03d    ×1      signed, –99 to +99 (max configured flap deg)
//   62       4     gOnsetRate          %+04d    ×100    signed, –999 to +999
//   66       2     spinRecoveryCue     %+02d    ×1      signed, –9 to +9
//   68       2     dataMark            %02u     ×1      unsigned, 0–99
//   70       2     pipPctLift          %02u     ×1      unsigned, 0–99 (visual L/Dmax pip — aerodynamic reference, lerp clean→fullflap)
//   72       2     checksum            ASCII hex        sum of bytes 0–71 & 0xFF
//   74       2     terminator          CR LF    —       0x0D 0x0A
//
// Design intent — the percent-lift contract:
//   Percent-lift is the honest single-linear envelope fraction:
//     percentLift = (AOA − α₀) / (α_stall − α₀) × 100, clamped to [0, 99]
//   The four band-edge percents (`tonesOnPctLift`, `onSpeedFastPctLift`,
//   `onSpeedSlowPctLift`, `stallWarnPctLift`) are the per-flap setpoints
//   put through the same formula.  Each one varies per flap because the
//   underlying body angles vary per flap.  Consumers should use these
//   percents as the band anchors when rendering an indexer or pip — the
//   wire never carries body angles for AOA.
//
// Design intent — operational vs aerodynamic cues (Vac, ld_max.pdf §8):
//   "L/Dmax pips are aerodynamic references. Fast tone is an
//    operational limit cue. They must remain independent."
//   The wire reflects this with two separate fields:
//     * tonesOnPctLift — operational. Snapped to the active detent's
//       L/Dmax percent. Drives the M5 bottom-chevron gate and matches
//       the audio low-tone threshold exactly. Snaps at iIndex
//       transitions in lockstep with the audio path.
//     * pipPctLift     — aerodynamic. Linearly interpolated in lever-pot
//       space between the cleanest detent's L/Dmax percent and the
//       most-deployed detent's OnSpeed-band center. Slides smoothly as
//       the lever moves; does not depend on the active detent index.
//   The two coincide visually only at the cleanest-detent endpoint.
//
//   See onspeed_core/aoa/PercentLift.h for the formula and
//   docs/site/docs/reference/serial-protocol.md for the wire reference.
//
// Wire-format invariant: the exact ASCII bytes emitted by BuildFrame must
// be byte-for-byte identical to those emitted by the Gen3 firmware's
// DisplaySerial::Write() for the same input values. Any change to offsets,
// widths, scales, or clamp ranges is a protocol change and requires a
// simultaneous flash of both firmwares.
//
// Used by:
//   Gen3 firmware: software/sketch_common/src/io/DisplaySerial.cpp
//   M5 firmware:   software/OnSpeed-M5-Display/src/SerialRead.cpp

#ifndef ONSPEED_CORE_PROTO_DISPLAY_SERIAL_H
#define ONSPEED_CORE_PROTO_DISPLAY_SERIAL_H

#include <cstddef>
#include <cstdint>
#include <optional>

namespace onspeed::proto {

/// Total length of a complete #1 frame in bytes (including CRLF terminator).
/// v4.21 was 74 bytes; v4.22 is 76 (adds pipPctLift at offset 70).
inline constexpr size_t kDisplayFrameSizeBytes = 76;

/// Length of the ASCII payload that the checksum covers (bytes 0–71 inclusive).
inline constexpr size_t kDisplayFrameChecksumLen = 72;

/// Nominal period between frames (milliseconds). Matches
/// kDisplaySerialPeriodMs in the Gen3 firmware's HardwareMap.h.
inline constexpr int kDisplayFramePeriodMs = 50;

// ============================================================================
// BuildInputs — values fed to BuildFrame.
//
// All values are in engineering units. BuildFrame applies the wire-format
// scale factors, sign conventions, and clamp ranges.
//
// Convention: lateralG is in BALL-FRAME (positive = leftward), not
//   body-frame.  This is the canonical reference for the dual-frame
//   sign decision; every other code site (M5 SerialRead, JS slipBall,
//   web-socket-protocol.md) refers back here.
//
//   Physics: when the body yaws right, the free-floating ball in the
//   slip-skid tube has its own inertia and is "left behind" by the
//   moving tube — so the ball deflects LEFT.  The wire encodes that
//   ball-frame view directly, and `Slip = LateralG × 850` in the M5
//   consumer plots ball position with no further sign work.
//
//   Mechanically this is just `−AccelLatFilter` from the producer's
//   IMU body-Y axis; the Gen3 builder applies the negation here at
//   the wire-format boundary.  The WebSocket JSON's `lateralGLoad`
//   field ships the same source un-negated (body-frame, positive =
//   right) for numeric "Lat G" readouts — JS slip-ball renderers
//   reading JSON must flip the sign locally to recover ball-frame.
// ============================================================================

struct DisplayBuildInputs {
    float pitchDeg           = 0.0f;  // smoothed pitch (deg)
    float rollDeg            = 0.0f;  // smoothed roll (deg)
    float iasKt              = 0.0f;  // indicated airspeed (kt); 0 when below mute threshold
    float paltFt             = 0.0f;  // pressure altitude (ft)
    float turnRateDps        = 0.0f;  // yaw rate (deg/s)
    float lateralG           = 0.0f;  // lateral acceleration, negated (g); positive = leftward
    float verticalGScaled10  = 0.0f;  // vertical G × 10, already rounded to nearest tenth (raw int, stored as float)
    int   percentLift        = 0;     // current AOA expressed as honest envelope fraction, 0–99
    int   vsiFpm10           = 0;     // vsi / 10 (fpm), floored — range –999..+999
    int   oatC               = 0;     // OAT (°C); only valid when OAT sensor enabled
    float flightPathDeg      = 0.0f;  // flight-path angle (deg)
    int   flapsDeg           = 0;     // flap position (deg) with sign
    int   tonesOnPctLift     = 0;     // active-detent L/Dmax pct; snapped per detent (operational, audio gate)
    int   onSpeedFastPctLift = 0;     // OnSpeedFast body angle through ComputePercentLift, 0–99
    int   onSpeedSlowPctLift = 0;     // OnSpeedSlow body angle through ComputePercentLift, 0–99
    int   stallWarnPctLift   = 0;     // StallWarn body angle through ComputePercentLift, 0–99
    int   flapsMinDeg        = 0;     // minimum configured flap deg (lever full retract)
    int   flapsMaxDeg        = 0;     // maximum configured flap deg (lever full extend)
    float gOnsetRate         = 0.0f;  // G onset rate (g/s); positive = G load increasing
    int   spinRecoveryCue    = 0;     // –1 / 0 / +1, currently always 0 (reserved for future spin-recovery cue logic)
    int   dataMark           = 0;     // data mark 0–99 (wraps mod 100)
    int   pipPctLift         = 0;     // visual L/Dmax pip; lerp clean→fullflap (aerodynamic reference)
};

// ============================================================================
// DisplayFrame — parsed output from ParseFrame.
//
// Values are in engineering units after reversing the wire-format scale
// factors. Field names mirror the M5 firmware's public variables.
// ============================================================================

struct DisplayFrame {
    float pitchDeg           = 0.0f;
    float rollDeg            = 0.0f;
    float iasKt              = 0.0f;
    float paltFt             = 0.0f;
    float turnRateDps        = 0.0f;
    float lateralG           = 0.0f;  // negated (see BuildInputs convention)
    float verticalG          = 0.0f;  // in g (wire value / 10)
    int   percentLift        = 0;
    float vsiFpm             = 0.0f;  // in fpm (wire value × 10)
    int   oatC               = 0;
    float flightPathDeg      = 0.0f;
    int   flapsDeg           = 0;
    int   tonesOnPctLift     = 0;
    int   onSpeedFastPctLift = 0;
    int   onSpeedSlowPctLift = 0;
    int   stallWarnPctLift   = 0;
    int   flapsMinDeg        = 0;
    int   flapsMaxDeg        = 0;
    float gOnsetRate         = 0.0f;
    int   spinRecoveryCue    = 0;
    int   dataMark           = 0;
    int   pipPctLift         = 0;     // v4.22+, see field comment in DisplayBuildInputs
};

// ============================================================================
// BuildFrame
//
// Encodes `in` into a complete kDisplayFrameSizeBytes-byte #1 frame and
// writes it into `out`.  `out` must point to at least
// kDisplayFrameSizeBytes bytes of writable storage.
//
// Returns kDisplayFrameSizeBytes on success.
// Returns 0 if the snprintf payload is not exactly kDisplayFrameChecksumLen
// characters (indicates a clamping or format bug — should never happen with
// valid inputs).
// ============================================================================

size_t BuildDisplayFrame(const DisplayBuildInputs& in,
                         uint8_t*                  out,
                         size_t                    out_capacity);

// ============================================================================
// ParseFrame
//
// Parses a kDisplayFrameSizeBytes-byte #1 frame from `buf`.
// Returns std::nullopt if:
//   - len < kDisplayFrameSizeBytes
//   - magic bytes are not "#1"
//   - checksum does not match
//   - any numeric field fails to parse
// ============================================================================

std::optional<DisplayFrame> ParseDisplayFrame(const uint8_t* buf, size_t len);

// ============================================================================
// DisplayFrameAccumulator — byte-stream framing
//
// State machine that consumes the byte stream emitted by Gen3's
// DisplaySerial::Write() one byte at a time, returning a parsed
// DisplayFrame on the byte that completes a valid frame.
//
// Logic:
//   - Any '#' byte resets the accumulator to start-of-frame.
//   - After start-of-frame, bytes are accumulated until the buffer
//     reaches kDisplayFrameSizeBytes.
//   - On the final byte (LF), if the buffer starts with "#1" and ends
//     with CRLF, ParseDisplayFrame is invoked; success returns the
//     frame, failure returns nullopt and resets.
//   - Any byte that would overflow the buffer resets the accumulator.
//
// Lives in onspeed_core (not in the M5 firmware) so the byte-stream
// framing is exercised by the same native test suite that pins the
// wire format. Without this lifting, a future field addition can
// quietly break the M5's hardcoded frame-length checks while
// BuildDisplayFrame / ParseDisplayFrame round-trips keep passing.
// ============================================================================

class DisplayFrameAccumulator {
public:
    DisplayFrameAccumulator();

    /// Reset the accumulator to start-of-frame state.
    void Reset();

    /// Feed one byte. Returns a parsed frame on the byte that
    /// completes a valid #1 frame; otherwise nullopt.
    std::optional<DisplayFrame> Inject(uint8_t byte);

    /// True if a partial frame is currently being accumulated.
    bool InProgress() const { return length_ > 0; }

    /// Number of bytes currently in the buffer.
    size_t Length() const { return length_; }

private:
    uint8_t buffer_[kDisplayFrameSizeBytes];
    size_t  length_;
};

}   // namespace onspeed::proto

#endif  // ONSPEED_CORE_PROTO_DISPLAY_SERIAL_H
