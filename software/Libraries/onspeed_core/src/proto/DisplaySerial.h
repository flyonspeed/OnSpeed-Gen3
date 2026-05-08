// proto/DisplaySerial.h — OnSpeed `#1` display-serial protocol.
//
// This is the single source of truth for the wire format of the 77-byte ASCII
// frame exchanged between the Gen3 main firmware (producer) and the M5Stack
// secondary display firmware (consumer). (v4.23 size; was 76 bytes in v4.22.)
//
// Frame format (77 bytes total, v4.23):
//
//   Offset  Width  Field               Format   Scale   Notes
//   ------  -----  ------------------  -------  ------  ----------------------
//    0       2     magic               literal  —       "#1"
//    2       4     pitchDeg            %+04d    ×10     signed, –999 to +999
//    6       5     rollDeg             %+05d    ×10     signed, –9999 to +9999
//   11       4     iasKt               %04u     ×10     unsigned, 0–9999 (9999 = sentinel "air data not valid"; see iasValid below)
//   15       6     paltFt              %+06d    ×1      signed, –99999 to +99999
//   21       5     turnRateDps         %+05d    ×10     signed, –9999 to +9999
//   26       3     lateralG            %+03d    ×100    signed, –99 to +99 (body-frame, +rightward)
//   29       3     verticalG           %+03d    ×10     signed, –99 to +99
//   32       3     percentLift         %03u     ×10     unsigned, 0–999 (wire-tenths of a percent; consumers see whole-percent float, `473` = 47.3%)
//   35       4     vsiFpm10            %+04d    ×1      vsi_fpm/10, –999 to +999
//   39       3     oatC                %+03d    ×1      signed, –99 to +99
//   42       4     flightPathDeg       %+04d    ×10     signed, –999 to +999
//   46       3     flapsDeg            %+03d    ×1      signed, –99 to +99
//   49       2     tonesOnPctLift      %02u     ×1      unsigned, 0–99 (active-detent L/Dmax — operational audio gate, snapped per detent)
//   51       2     onSpeedFastPctLift  %02u     ×1      unsigned, 0–99 (OnSpeedFast body angle through the percent-lift formula)
//   53       2     onSpeedSlowPctLift  %02u     ×1      unsigned, 0–99 (OnSpeedSlow body angle through the percent-lift formula)
//   55       2     stallWarnPctLift    %02u     ×1      unsigned, 0–99 (StallWarn body angle through the percent-lift formula)
//   57       3     flapsMinDeg         %+03d    ×1      signed, –99 to +99 (min configured flap deg)
//   60       3     flapsMaxDeg         %+03d    ×1      signed, –99 to +99 (max configured flap deg)
//   63       4     gOnsetRate          %+04d    ×100    signed, –999 to +999
//   67       2     spinRecoveryCue     %+02d    ×1      signed, –9 to +9
//   69       2     dataMark            %02u     ×1      unsigned, 0–99
//   71       2     pipPctLift          %02u     ×1      unsigned, 0–99 (visual L/Dmax pip — aerodynamic reference, lerp clean→fullflap)
//   73       2     checksum            ASCII hex        sum of bytes 0–72 & 0xFF
//   75       2     terminator          CR LF    —       0x0D 0x0A
//
// Design intent — air-data validity (iasValid):
//   Air-data-derived fields (iasKt, percentLift) ride on a single
//   producer-side validity flag, `DisplayBuildInputs::iasValid`.  The
//   producer derives this from `bIasAlive` — the sensor-level air-data
//   validity flag (rising-edge 20 kt, falling-edge 15 kt, hysteresis;
//   see onspeed_core/sensors/IasAlive.h).  When iasValid is false:
//     * iasKt encodes as the wire sentinel `9999` (the maximum value
//       of the %04u field; well above any operational airspeed).
//     * percentLift encodes as `0` (which is also the "uncalibrated"
//       value when no flap snapshot is available — the consumer must
//       use `iasIsValid` to distinguish the two cases when rendering).
//   Consumers (M5 SerialRead.cpp) detect the sentinel by checking
//   `DisplayFrame::iasIsValid` and render dashes ("--") in place of
//   IAS / percentLift digits.  This is independent of the audio mute
//   threshold (`iMuteAudioUnderIAS`); the pilot's audio-mute knob
//   controls when tones play, not whether displayed values are
//   trustworthy.  See issue #358 for the rationale.
//
// Design intent — the percent-lift contract:
//   Percent-lift is the honest single-linear envelope fraction:
//     percentLift = (AOA − α₀) / (α_stall − α₀) × 100, clamped to [0.0, 99.9]
//   On the wire, the current AOA is encoded as `int(percent × 10)` in a
//   `%03u` field (range 0..999) so the consumer's index bar can render at
//   sub-pixel temporal smoothness off the 20 Hz frame cadence.  In memory,
//   producers set `DisplayBuildInputs::percentLiftPct` (float, whole
//   percent) and parsers read `DisplayFrame::percentLiftPct` (float, whole
//   percent) — the wire's tenths encoding is an implementation detail of
//   the encode/decode boundary.  The four band-edge percents
//   (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`,
//   `stallWarnPctLift`) are the per-flap setpoints put through the same
//   formula at integer-percent resolution (0..99) — they only move at
//   detent-snap or config-save events, so sub-percent resolution buys
//   nothing on those.  Each band-edge varies per flap because the
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
//       most-deployed detent's bottom-half-of-donut target
//       ((3*fast + slow) / 4). Slides smoothly as the lever moves; does
//       not depend on the active detent index.
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
/// v4.21 was 74 bytes; v4.22 was 76 (added pipPctLift at offset 70);
/// v4.23 is 77 (widens percentLift to 3 chars carrying tenths of a percent).
inline constexpr size_t kDisplayFrameSizeBytes = 77;

/// Sentinel emitted in the iasKt field when the producer's bIasAlive
/// flag is false (i.e. air-data is not yet valid: pitot pressure inside
/// the noise floor, or rising threshold not yet crossed).  Picked as
/// the maximum of the %04u field — far above any operational airspeed
/// (the wire's nominal range is 0..999.9 kt) — so a consumer that does
/// not know about iasIsValid still produces an obviously bogus rather
/// than a plausibly low IAS reading.
inline constexpr uint16_t kIasInvalidWireSentinel = 9999;

/// Length of the ASCII payload that the checksum covers (bytes 0–72 inclusive).
inline constexpr size_t kDisplayFrameChecksumLen = 73;

/// Nominal period between frames (milliseconds). Matches
/// kDisplaySerialPeriodMs in the Gen3 firmware's HardwareMap.h.
inline constexpr int kDisplayFramePeriodMs = 50;

// ============================================================================
// BuildInputs — values fed to BuildFrame.
//
// All values are in engineering units. BuildFrame applies the wire-format
// scale factors, sign conventions, and clamp ranges.
//
// Convention: lateralG is in BODY-FRAME at v4.23 (positive = airframe
//   accelerating rightward).  Matches the IMU's body-Y axis, the SD
//   log's `imuLateralG` column, and the WebSocket JSON's `lateralGLoad`
//   field — every transport now uses the same sign convention.  This
//   is the canonical reference for the convention; every other code
//   site (M5 SerialRead, JS slipBall, web-socket-protocol.md) refers
//   back here.
//
//   Physics: when the body yaws right, the free-floating ball in the
//   slip-skid tube has its own inertia and is "left behind" by the
//   moving tube — so the ball deflects LEFT.  Slip-skid ball renderers
//   (the M5's SerialRead::SerialProcess and the LiveView's slipBall.js)
//   negate locally at the rendering site to recover ball-frame
//   (positive = ball drawn right of center).
//
//   The Gen3 builder transmits `g_AHRS.AccelLatFilter.get()` directly
//   — no negation at the wire-format boundary.  See
//   LATERAL_G_CONVENTION.md for the full chain of how this value flows
//   through every surface.
// ============================================================================

struct DisplayBuildInputs {
    float pitchDeg           = 0.0f;  // smoothed pitch (deg)
    float rollDeg            = 0.0f;  // smoothed roll (deg)
    float iasKt              = 0.0f;  // indicated airspeed (kt); honored when iasValid=true, ignored when iasValid=false (encoder writes 9999 sentinel)
    bool  iasValid           = true;  // when false: encoder emits the 9999 sentinel for iasKt and 0 for percentLift; consumers render "--"
    float paltFt             = 0.0f;  // pressure altitude (ft)
    float turnRateDps        = 0.0f;  // yaw rate (deg/s)
    float lateralG           = 0.0f;  // lateral acceleration (g); body-frame, positive = airframe accel rightward
    float verticalGScaled10  = 0.0f;  // vertical G × 10, already rounded to nearest tenth (raw int, stored as float)
    float percentLiftPct     = 0.0f;  // current AOA, whole percent (0.0–99.9); BuildFrame emits as int(pct × 10) in %03u
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
    float iasKt              = 0.0f;  // raw decoded value; ignore when iasIsValid=false (sentinel was on the wire)
    bool  iasIsValid         = true;  // false when the wire's iasKt field carried the 9999 sentinel — render "--" for iasKt and percentLift
    float paltFt             = 0.0f;
    float turnRateDps        = 0.0f;
    float lateralG           = 0.0f;  // body-frame (see BuildInputs convention)
    float verticalG          = 0.0f;  // in g (wire value / 10)
    float percentLiftPct     = 0.0f;  // whole percent (0.0–99.9); ParseFrame divides the wire's tenths field by 10
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
