// Vn300.h
//
// VectorNav VN-300 binary serial protocol parser.
//
// Wire format: 138-byte frame, four active binary-output groups
// (Common + Time + GNSS1 + AHRS).
//
//   Sync:        0xFA (byte 0)
//   Groups byte: 0x1B at byte 1 (bits 0,1,3,4 = Common+Time+GNSS1+AHRS)
//   Group masks: 8 bytes at [2..9] = Common(LE) Time(LE) GNSS1(LE) AHRS(LE)
//   Payload:     126 bytes at [10..135]
//   CRC:         2 bytes (bytes 136..137), computed over bytes 1..137
//
// Expected 10-byte header bytes [0..9]:
//   {0xFA, 0x1B, 0xE3, 0x01, 0x00, 0x02, 0x90, 0x00, 0x42, 0x01}
//
//   Common mask 0x01E3 (bits 0,1,5,6,7,8): TimeStartup + TimeGps +
//     AngularRate + Position + Velocity + Accel
//   Time mask 0x0200 (bit 9): TimeStatus
//   GNSS1 mask 0x0090 (bits 4,7): Fix + VelNed
//   AHRS mask 0x0142 (bits 1,6,8): YawPitchRoll + LinearAccelBody + YprU
//
// Field offsets (138-byte frame, little-endian):
//   [10..17]   TimeStartup        u64 (ns since VN-300 boot)
//   [18..25]   TimeGps            u64 (ns since GPS epoch 1980)
//   [26..29]   AngularRateRoll    f32
//   [30..33]   AngularRatePitch
//   [34..37]   AngularRateYaw
//   [38..45]   GnssLat            f64
//   [46..53]   GnssLon            f64
//   [54..61]   EstAltMeters       f64 (INS-fused altitude from Common.Position)
//   [62..65]   VelNedNorth        f32
//   [66..69]   VelNedEast
//   [70..73]   VelNedDown
//   [74..77]   AccelFwd           f32
//   [78..81]   AccelLat
//   [82..85]   AccelVert
//   [86]       TimeStatus         u8
//   [87]       GpsFix             u8
//   [88..91]   GnssVelNedNorth    f32
//   [92..95]   GnssVelNedEast
//   [96..99]   GnssVelNedDown
//   [100..103] Yaw                f32
//   [104..107] Pitch
//   [108..111] Roll
//   [112..115] LinAccFwd          f32
//   [116..119] LinAccLat
//   [120..123] LinAccVert
//   [124..127] YawSigma           f32
//   [128..131] PitchSigma
//   [132..135] RollSigma
//   [136..137] CRC-16 (big-endian)
//
// TimeStatus bit layout (UM005 §5.5.10):
//   bit0 = timeOk      (GpsTow within an acceptable range; clock disciplined)
//   bit1 = dateOk      (TimeGps valid — GPS week resolved)
//   bit2 = utcTimeValid
//
// CRC algorithm (VN CRC-16):
//   For each byte i from 1 to N-2 (inclusive):
//     crc = (crc >> 8) | (crc << 8);
//     crc ^= byte[i];
//     crc ^= (crc & 0xFF) >> 4;
//     crc ^= (crc << 8) << 4;
//     crc ^= ((crc & 0xFF) << 4) << 1;
//   Valid if crc == 0 after including the two CRC bytes.
//
// The EfisFrame type carries only attitude/airspeed/etc; the additional
// VN-300 fields (gyro rates, GPS coords, sigmas, per-sample timestamps)
// are exposed on Vn300Data via TakeVn300Data() / TryTakeVn300Data().

#ifndef ONSPEED_CORE_EFIS_VN300_H
#define ONSPEED_CORE_EFIS_VN300_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>

namespace onspeed::efis {

// Extended VN-300 fields not present in EfisFrame.
// Consumers that need these (DataServer, LogSensor) retrieve them via
// Vn300Parser::TakeVn300Data().
struct Vn300Data {
    float        angularRateRoll    = 0.0f;
    float        angularRatePitch   = 0.0f;
    float        angularRateYaw     = 0.0f;
    float        velNedNorth        = 0.0f;
    float        velNedEast         = 0.0f;
    float        velNedDown         = 0.0f;
    float        accelFwd           = 0.0f;
    float        accelLat           = 0.0f;
    float        accelVert          = 0.0f;
    float        yaw                = 0.0f;
    float        pitch              = 0.0f;
    float        roll               = 0.0f;
    float        linAccFwd          = 0.0f;
    float        linAccLat          = 0.0f;
    float        linAccVert         = 0.0f;
    float        yawSigma           = 0.0f;
    float        rollSigma          = 0.0f;
    float        pitchSigma         = 0.0f;
    float        gnssVelNedNorth    = 0.0f;
    float        gnssVelNedEast     = 0.0f;
    float        gnssVelNedDown     = 0.0f;
    double       gnssLat            = 0.0;
    double       gnssLon            = 0.0;
    // INS-estimated altitude (metres) from the Common.Position group.
    // Sensor-fused GPS+IMU LLA, not raw GNSS altitude.
    double       estAltMeters       = 0.0;
    uint8_t      gpsFix             = 0;
    // Per-sample timestamps from the VN-300 Common group (stamped at IMU
    // sample time, not GNSS solution time).
    uint64_t     timeStartupNs      = 0;   // ns since VN-300 boot
    uint64_t     timeGpsNs          = 0;   // ns since GPS epoch; valid iff
                                            // (timeStatus & 0x02) set
    uint8_t      timeStatus         = 0;
};

// Vn300Parser — state machine for VectorNav VN-300 binary output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// for a normalised EfisFrame and TakeVn300Data() for the full VN-300 dataset.
// Both are cleared by the respective Take calls.
//
// State machine:
//   Waiting for sync: looks for the sequence 0xFA followed by 0x1B
//                     (the new Groups byte for Common+Time+GNSS1+AHRS).
//   Collecting: accumulates bytes until bufLen_ == kPacketSize.
//   On completion: validates header + CRC, then decodes.
// Vn300Diagnostics — running counts of where the parser falls off the wire.
//
// Production builds NEVER read these; they're a debug aid for bench bring-up
// of the EFIS wire (cable polarity, baud match, voltage levels, etc.). When
// the parser is healthy, decoded == sync2 == bytesFed / 138 (approximately).
// When something is wrong, the imbalance tells you WHERE it broke:
//
//   bytesFed > 0 but sync1 == 0:      no 0xFA in the stream — wrong baud,
//                                     inverted polarity, or wire dead.
//   sync1 > 0 but sync2 == 0:         0xFA arrives but 0x1B never follows —
//                                     framing wrong (likely garbage bytes).
//   sync2 > 0 but headerFail growing: sync pattern seen by chance, but the
//                                     group-mask bytes that follow don't
//                                     match — wire is delivering corrupted
//                                     data after a chance-match.
//   sync2 == headerOk but crcFail > 0:header is right, byte values aren't —
//                                     line noise, or a baud-rate mismatch
//                                     just close enough to align headers
//                                     but not payloads.
//   crcOk > 0:                        frames decoding cleanly. Healthy.
struct Vn300Diagnostics {
    uint64_t bytesFed     = 0;   // every FeedByte() call increments this
    uint32_t sync1        = 0;   // count of times we saw byte == 0xFA
    uint32_t sync2        = 0;   // count of times we saw 0xFA followed by 0x1B
    uint32_t headerOk     = 0;   // 10-byte header memcmp matched
    uint32_t headerFail   = 0;   // collected 138 bytes but header memcmp failed
    uint32_t crcOk        = 0;   // header AND CRC passed
    uint32_t crcFail      = 0;   // header matched but CRC failed
    // First-byte distribution: counts of each value seen as the FIRST byte
    // following a NON-in-progress state. Tells us what bytes are arriving
    // when we're "between frames" — if a real VN-300 is on the wire, this
    // should be dominated by 0xFA (start of next frame). If it's a uniform
    // distribution, we're reading noise. We sample only 16 buckets across
    // the 256-value space (b >> 4) so the struct stays small.
    uint32_t firstByteByNibble[16] = {};
};

class Vn300Parser {
public:
    Vn300Parser() = default;

    void FeedByte(uint8_t b);
    bool TryTakeFrame(EfisFrame& out);
    bool TryTakeVn300Data(Vn300Data& out);
    std::optional<EfisFrame>  TakeFrame();
    std::optional<Vn300Data>  TakeVn300Data();
    void Reset();

    // Snapshot the running diagnostics. Cheap (struct copy). Caller is
    // responsible for periodic emission / rate limiting.
    const Vn300Diagnostics& Diag() const { return diag_; }

    static constexpr int kPacketSize = 138;

private:
    uint8_t  buf_[kPacketSize] = {};
    int      bufLen_            = 0;
    bool     inProgress_        = false;
    uint8_t  prevByte_          = 0;

    EfisFrame  pendingFrame_;
    Vn300Data  pendingData_;
    bool       pendingFrameReady_ = false;
    bool       pendingDataReady_  = false;

    Vn300Diagnostics diag_;

    void Decode();
};

}   // namespace onspeed::efis

#endif
