// proto/DisplaySerial.h — OnSpeed `#1` display-serial protocol.
//
// This is the single source of truth for the wire format of the 80-byte ASCII
// frame exchanged between the Gen3 main firmware (producer) and the M5Stack
// secondary display firmware (consumer).
//
// Frame format (80 bytes total):
//
//   Offset  Width  Field              Format   Scale   Notes
//   ------  -----  -----------------  -------  ------  ----------------------
//    0       2     magic              literal  —       "#1"
//    2       4     pitchDeg           %+04d    ×10     signed, –999 to +999
//    6       5     rollDeg            %+05d    ×10     signed, –9999 to +9999
//   11       4     iasKt              %04u     ×10     unsigned, 0–9999
//   15       6     paltFt             %+06d    ×1      signed, –99999 to +99999
//   21       5     turnRateDps        %+05d    ×10     signed, –9999 to +9999
//   26       3     lateralG           %+03d    ×100    signed, –99 to +99
//   29       3     verticalG          %+03d    ×10     signed, –99 to +99
//   32       2     percentLift        %02u     ×1      unsigned, 0–99
//   34       4     aoaDeg             %+04d    ×10     signed, –999 to +999
//   38       4     vsiFpm10           %+04d    ×1      vsi_fpm/10, –999 to +999
//   42       3     oatC               %+03d    ×1      signed, –99 to +99
//   45       4     flightPathDeg      %+04d    ×10     signed, –999 to +999
//   49       3     flapsDeg           %+03d    ×1      signed, –99 to +99
//   52       4     stallWarnAoaDeg    %+04d    ×10     signed, –999 to +999
//   56       4     onSpeedSlowAoaDeg  %+04d    ×10     signed, –999 to +999
//   60       4     onSpeedFastAoaDeg  %+04d    ×10     signed, –999 to +999
//   64       4     tonesOnAoaDeg      %+04d    ×10     signed, –999 to +999
//   68       4     gOnsetRate         %+04d    ×100    signed, –999 to +999
//   72       2     spinRecoveryCue    %+02d    ×1      signed, –9 to +9
//   74       2     dataMark           %02u     ×1      unsigned, 0–99
//   76       2     checksum           ASCII hex        sum of bytes 0–75 & 0xFF
//   78       2     terminator         CR LF    —       0x0D 0x0A
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
inline constexpr size_t kDisplayFrameSizeBytes = 80;

/// Length of the ASCII payload that the checksum covers (bytes 0–75 inclusive).
inline constexpr size_t kDisplayFrameChecksumLen = 76;

/// Nominal period between frames (milliseconds). Matches
/// kDisplaySerialPeriodMs in the Gen3 firmware's HardwareMap.h.
inline constexpr int kDisplayFramePeriodMs = 50;

// ============================================================================
// BuildInputs — values fed to BuildFrame.
//
// All values are in engineering units. BuildFrame applies the wire-format
// scale factors, sign conventions, and clamp ranges.
//
// Convention: lateralG is the negated lateral acceleration.
//   The Gen3 builder passes `–AccelLatFilter.get()` so that positive wire
//   values mean leftward. The M5 parser stores it as-is. BuildInputs holds
//   the already-negated value, matching the Gen3 builder's local variable
//   `iLatG100 = SafeScaledInt(-g_AHRS.AccelLatFilter.get(), 100.0f, ...)`.
// ============================================================================

struct DisplayBuildInputs {
    float pitchDeg          = 0.0f;  // smoothed pitch (deg)
    float rollDeg           = 0.0f;  // smoothed roll (deg)
    float iasKt             = 0.0f;  // indicated airspeed (kt); 0 when below mute threshold
    float paltFt            = 0.0f;  // pressure altitude (ft)
    float turnRateDps       = 0.0f;  // yaw rate (deg/s)
    float lateralG          = 0.0f;  // lateral acceleration, negated (g); positive = leftward
    float verticalGScaled10 = 0.0f;  // vertical G × 10, already ceil'd (raw int, stored as float)
    int   percentLift       = 0;     // 0–99
    float aoaDeg            = 0.0f;  // AOA (deg); 0 when below mute threshold
    int   vsiFpm10          = 0;     // vsi / 10 (fpm), floored — range –999..+999
    int   oatC              = 0;     // OAT (°C); only valid when OAT sensor enabled
    float flightPathDeg     = 0.0f;  // flight-path angle (deg)
    int   flapsDeg          = 0;     // flap position (deg) with sign
    float stallWarnAoaDeg   = 0.0f;  // stall-warning AOA setpoint (deg)
    float onSpeedSlowAoaDeg = 0.0f;  // on-speed slow AOA setpoint (deg)
    float onSpeedFastAoaDeg = 0.0f;  // on-speed fast AOA setpoint (deg)
    float tonesOnAoaDeg     = 0.0f;  // tones-on AOA setpoint (deg); sourced from per-flap fLDMAXAOA config
    float gOnsetRate        = 0.0f;  // G onset rate (G/s), currently always 0.0
    int   spinRecoveryCue   = 0;     // –1 / 0 / +1, currently always 0
    int   dataMark          = 0;     // data mark 0–99 (wraps mod 100)
};

// ============================================================================
// DisplayFrame — parsed output from ParseFrame.
//
// Values are in engineering units after reversing the wire-format scale
// factors. Field names mirror the M5 firmware's public variables.
// ============================================================================

struct DisplayFrame {
    float pitchDeg          = 0.0f;
    float rollDeg           = 0.0f;
    float iasKt             = 0.0f;
    float paltFt            = 0.0f;
    float turnRateDps       = 0.0f;
    float lateralG          = 0.0f;  // negated (see BuildInputs convention)
    float verticalG         = 0.0f;  // in g (wire value / 10)
    int   percentLift       = 0;
    float aoaDeg            = 0.0f;
    float vsiFpm            = 0.0f;  // in fpm (wire value × 10)
    int   oatC              = 0;
    float flightPathDeg     = 0.0f;
    int   flapsDeg          = 0;
    float stallWarnAoaDeg   = 0.0f;
    float onSpeedSlowAoaDeg = 0.0f;
    float onSpeedFastAoaDeg = 0.0f;
    float tonesOnAoaDeg     = 0.0f;
    float gOnsetRate        = 0.0f;
    int   spinRecoveryCue   = 0;
    int   dataMark          = 0;
};

// ============================================================================
// BuildFrame
//
// Encodes `in` into a complete 80-byte #1 frame and writes it into `out`.
// `out` must point to at least kDisplayFrameSizeBytes bytes of writable
// storage.
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
// Parses a 80-byte #1 frame from `buf`.
// Returns std::nullopt if:
//   - len < kDisplayFrameSizeBytes
//   - magic bytes are not "#1"
//   - checksum does not match
//   - any numeric field fails to parse
// ============================================================================

std::optional<DisplayFrame> ParseDisplayFrame(const uint8_t* buf, size_t len);

}   // namespace onspeed::proto

#endif  // ONSPEED_CORE_PROTO_DISPLAY_SERIAL_H
