// Vn300.h
//
// VectorNav VN-300 (and VN-100) binary serial protocol parser.
//
// The VN-300 outputs a 127-byte binary frame:
//
//   Sync:   0xFA, 0x19  (two-byte sequence, detected on 2nd byte)
//   Header: 8 bytes (bytes 0..7)
//   Groups: 119 bytes of payload data
//   CRC:    2 bytes (bytes 125..126), computed over bytes 1..124
//
// The expected header is:
//   {0xFA, 0x19, 0xE0, 0x01, 0x91, 0x00, 0x42, 0x01}
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
// Fields extracted (all floats, little-endian):
//   AngularRate{Roll,Pitch,Yaw} at offsets 8/12/16
//   GnssLat (double) at 20, GnssLon (double) at 28,
//     altitude (double) at 36 — INS-estimated altitude in metres from the
//     Common.Position group (sensor-fused GPS+IMU LLA), not the raw GNSS
//     altitude that would come from a separate GPSGROUP_POSLLA selector
//   VelNed{North,East,Down} at 44/48/52
//   Accel{Fwd,Lat,Vert} at 56/60/64
//   GPS time bytes at 71..73 (Hour, Min, Sec) and 74..75 (Ms, u16 LE)
//   GPSFix at 76
//   GnssVelNed{North,East,Down} at 77/81/85
//   {Yaw,Pitch,Roll} at 89/93/97
//   LinAcc{Fwd,Lat,Vert} at 101/105/109
//   {Yaw,Pitch,Roll}Sigma at 113/117/121
//
// Note: the EfisFrame type carries only pitch, roll, IAS, TAS, Palt, VSI,
// OAT, AOA, heading, lateralG, and verticalG. The additional VN-300 fields
// (gyro rates, GPS coords, sigmas) are not in EfisFrame.
// After extraction to EfisFrame the consumer (EfisSerialPort / LogSensor /
// DataServer) still needs the raw VN-300 data for those extra fields.
//
// This is a known impedance mismatch: EfisFrame was designed for
// text-based EFIS types and does not have slots for IMU-grade data.
// The parser fills only the EfisFrame fields that have VN-300 equivalents;
// raw VN-300 data is also exposed via Vn300Data for consumers that need it.
//
// See: EfisSerial.cpp (EnVN300 branch) for original implementation.

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
    char         szTimeUTC[24]      = {};
};

// Vn300Parser — state machine for VectorNav VN-300/VN-100 binary output.
//
// Feed one byte at a time with FeedByte(). After each call, check TakeFrame()
// for a normalised EfisFrame and TakeVn300Data() for the full VN-300 dataset.
// Both are cleared by the respective Take calls.
//
// State machine:
//   Waiting for sync: looks for the sequence 0xFA followed by 0x19.
//   Collecting: accumulates bytes until bufLen_ == 127.
//   On completion: validates header + CRC, then decodes.
class Vn300Parser {
public:
    Vn300Parser() = default;

    void FeedByte(uint8_t b);
    std::optional<EfisFrame>  TakeFrame();
    std::optional<Vn300Data>  TakeVn300Data();
    void Reset();

private:
    static constexpr int kPacketSize = 127;

    uint8_t  buf_[kPacketSize] = {};
    int      bufLen_            = 0;
    bool     inProgress_        = false;
    uint8_t  prevByte_          = 0;

    std::optional<EfisFrame>  pendingFrame_;
    std::optional<Vn300Data>  pendingData_;

    void Decode();
};

}   // namespace onspeed::efis

#endif
