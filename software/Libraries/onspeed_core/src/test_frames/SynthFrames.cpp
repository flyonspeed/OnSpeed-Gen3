// SynthFrames.cpp — frame-construction helpers + cached static buffers.
//
// One TU because all three frame types share helpers (LE writes, CRC
// helpers); inlining them in the header isn't worth the duplication.

#include "SynthFrames.h"

#include <cstdio>
#include <cstring>

namespace onspeed::test_frames {

// ===========================================================================
// LE byte writers
// ===========================================================================
namespace {

void WriteFloatLE(uint8_t* out, std::size_t pos, float v) {
    std::memcpy(out + pos, &v, sizeof(float));
}
void WriteDoubleLE(uint8_t* out, std::size_t pos, double v) {
    std::memcpy(out + pos, &v, sizeof(double));
}
void WriteUint64LE(uint8_t* out, std::size_t pos, uint64_t v) {
    std::memcpy(out + pos, &v, sizeof(uint64_t));
}

// VN CRC-16, matches Vn300Parser exactly. [begin, end) inclusive/exclusive.
uint16_t ComputeVnCrc(const uint8_t* buf, std::size_t begin, std::size_t end) {
    uint16_t crc = 0;
    for (std::size_t i = begin; i < end; i++) {
        crc  = static_cast<uint16_t>((crc >> 8) | (crc << 8));
        crc ^= buf[i];
        crc ^= static_cast<uint16_t>(static_cast<uint8_t>(crc & 0xFF) >> 4);
        crc ^= static_cast<uint16_t>((crc << 8) << 4);
        crc ^= static_cast<uint16_t>(((crc & 0xFF) << 4) << 1);
    }
    return crc;
}

// Sum-of-bytes mod 256, written as 2 hex ASCII chars at crcPos[0..1].
// Used by SkyView and (in shifted form) by the boom builder.
void WriteHexCrc(uint8_t* buf, std::size_t crcPos, std::size_t sumStart,
                 std::size_t sumEnd) {
    int crc = 0;
    for (std::size_t i = sumStart; i < sumEnd; i++) crc += buf[i];
    crc &= 0xFF;
    char hex[3];
    std::snprintf(hex, sizeof(hex), "%02X", crc);
    buf[crcPos]     = static_cast<uint8_t>(hex[0]);
    buf[crcPos + 1] = static_cast<uint8_t>(hex[1]);
}

// ===========================================================================
// VN-300 — 138-B binary frame (issue #637: per-sample TimeStartup + TimeGps,
// four active groups: Common + Time + GNSS1 + AHRS).
// ===========================================================================
constexpr std::size_t kVn300Len = 138;

void BuildVn300(uint8_t* buf) {
    static const uint8_t kHeader[10] = {
        0xFA, 0x1B,           // sync + Groups (Common+Time+GNSS1+AHRS)
        0xE3, 0x01,           // Common mask 0x01E3 (LE)
        0x00, 0x02,           // Time mask   0x0200 (LE)
        0x90, 0x00,           // GNSS1 mask  0x0090 (LE)
        0x42, 0x01            // AHRS mask   0x0142 (LE)
    };
    std::memset(buf, 0, kVn300Len);
    std::memcpy(buf, kHeader, 10);

    // Per-sample timestamps (Common group leads the payload).
    WriteUint64LE(buf, 10, 1'234'567'890ULL);          // TimeStartup
    WriteUint64LE(buf, 18, 1'400'123'456'789'000ULL);  // TimeGps

    // Body angles — cruise-ish. See Vn300.cpp::Decode for offsets.
    WriteFloatLE(buf,  26, 0.0f);         // angular rates
    WriteFloatLE(buf,  30, 0.0f);
    WriteFloatLE(buf,  34, 0.0f);
    WriteDoubleLE(buf, 38,  40.0);        // lat
    WriteDoubleLE(buf, 46, -105.0);       // lon
    WriteDoubleLE(buf, 54, 1500.0);       // alt m
    WriteFloatLE(buf,  62, 50.0f);        // velNed N
    WriteFloatLE(buf,  66,  5.0f);        // velNed E
    WriteFloatLE(buf,  70, -1.0f);        // velNed D
    WriteFloatLE(buf,  74, 0.05f);        // accel fwd
    WriteFloatLE(buf,  78, 0.00f);        // accel lat
    WriteFloatLE(buf,  82, 1.00f);        // accel vert

    buf[86] = 0x07;                       // TimeStatus: all three valid bits
    buf[87] = 3;                          // GPSFix: 3D

    WriteFloatLE(buf,  88, 50.0f);        // gnssVelNed (mirror INS-velNed)
    WriteFloatLE(buf,  92,  5.0f);
    WriteFloatLE(buf,  96, -1.0f);
    WriteFloatLE(buf, 100, 90.0f);        // yaw
    WriteFloatLE(buf, 104,  2.5f);        // pitch
    WriteFloatLE(buf, 108,  0.5f);        // roll
    WriteFloatLE(buf, 112, 0.0f);         // linAcc
    WriteFloatLE(buf, 116, 0.0f);
    WriteFloatLE(buf, 120, 0.0f);
    WriteFloatLE(buf, 124, 0.5f);         // yaw sigma
    WriteFloatLE(buf, 128, 0.1f);         // pitch sigma
    WriteFloatLE(buf, 132, 0.1f);         // roll sigma

    const uint16_t crc = ComputeVnCrc(buf, 1, 136);
    buf[136] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    buf[137] = static_cast<uint8_t>(crc & 0xFF);
}

// ===========================================================================
// SkyView — !1 ADAHRS (74 B) + !3 EMS (225 B)
// ===========================================================================
constexpr std::size_t kSkyviewAdahrsLen = 74;
constexpr std::size_t kSkyviewEmsLen    = 225;

void WriteIntField(uint8_t* buf, std::size_t pos, std::size_t len, long val) {
    char tmp[16];
    if (val < 0 && len > 0) {
        std::snprintf(tmp, sizeof(tmp), "-%0*ld", static_cast<int>(len - 1), -val);
    } else {
        std::snprintf(tmp, sizeof(tmp), "%0*ld", static_cast<int>(len), val);
    }
    std::memcpy(buf + pos, tmp, len);
}

void WriteScaledField(uint8_t* buf, std::size_t pos, std::size_t len,
                      float val, float scale) {
    WriteIntField(buf, pos, len, static_cast<long>(val * scale));
}

void BuildSkyviewAdahrs(uint8_t* buf) {
    std::memset(buf, '0', kSkyviewAdahrsLen);
    buf[0] = '!';
    buf[1] = '1';
    buf[2] = '0';
    std::memcpy(buf + 3, "120000", 6);          // HHMMSS
    std::memcpy(buf + 9, "00", 2);              // fraction

    // Field offsets from DynonSkyview.cpp::DecodeAdahrs.
    WriteScaledField(buf, 11, 4,  2.5f, 10.0f);   // pitch
    WriteScaledField(buf, 15, 5,  0.5f, 10.0f);   // roll
    WriteIntField   (buf, 20, 3, 90L);            // heading
    WriteScaledField(buf, 23, 4, 95.0f, 10.0f);   // IAS
    WriteIntField   (buf, 27, 6, 5000L);          // palt
    // buf[33..36] not parsed.
    WriteScaledField(buf, 37, 3,  0.05f, 100.0f); // lateralG
    WriteScaledField(buf, 40, 3,  1.0f, 10.0f);   // verticalG
    WriteIntField   (buf, 43, 2, 45L);            // AOA %
    WriteScaledField(buf, 45, 4,  0.0f, 0.1f);    // VSI (raw × 10)
    WriteScaledField(buf, 49, 3, 15.0f, 1.0f);    // OAT
    WriteScaledField(buf, 52, 4, 100.0f, 10.0f);  // TAS
    // buf[56..69] not parsed.

    WriteHexCrc(buf, /*crcPos=*/70, /*sumStart=*/0, /*sumEnd=*/70);
    buf[72] = '\r';
    buf[73] = '\n';
}

void BuildSkyviewEms(uint8_t* buf) {
    std::memset(buf, '0', kSkyviewEmsLen);
    buf[0] = '!';
    buf[1] = '3';

    // Field offsets from DynonSkyview.cpp::DecodeEms.
    WriteScaledField(buf, 18, 4, 2500.0f, 1.0f);  // RPM
    WriteScaledField(buf, 26, 3,   25.0f, 10.0f); // MAP inHg
    WriteScaledField(buf, 29, 3,    8.0f, 10.0f); // fuel flow
    WriteScaledField(buf, 44, 3,   30.0f, 10.0f); // fuel remaining
    WriteScaledField(buf, 217, 3,  70.0f, 1.0f);  // % power

    WriteHexCrc(buf, /*crcPos=*/221, /*sumStart=*/0, /*sumEnd=*/221);
    buf[223] = '\r';
    buf[224] = '\n';
}

// ===========================================================================
// Boom — $AIRDAQ,64.12.B8,ADC,SSSS,DDDD,AAAA,BBBB,XX\r\n
// ===========================================================================
//
// Matches the real-world wire format from Sam's bench captures (see
// ~/Downloads/boomchecksum.txt). The legacy synth used a fictional
// "$BOOM,...,*XX" format with '*' as the CRC separator; real boom
// hardware uses ',' as the separator and the prefix is "$AIRDAQ".
// Both forms parse correctly via onspeed::boom::Decode (which is
// agnostic to the separator byte — it just reads CRC at [len-2..len-1]
// after stripping CR/LF, with the byte at [len-3] being whatever
// separator the producer chose).
//
// Frame anchor: BoomSerial.cpp / onspeed::boom::Decode require:
//   buf[0] == '$', total length >= 21, then 4 comma-separated ASCII
//   integer fields starting at byte 21, CRC at bytes [len-2..len-1].
//
// Prefix "$AIRDAQ,64.12.B8,ADC," is exactly 21 chars (positions 0..20),
// so the first integer field starts at position 21 — matching the
// parser anchor.

constexpr std::size_t kBoomMaxLen = 64;

std::size_t BuildBoom(uint8_t* buf) {
    // Prefix is exactly 21 chars (positions 0..20). First int at pos 21.
    // Trailing ',' before the CRC matches real $AIRDAQ wire format.
    const int n = std::snprintf(
        reinterpret_cast<char*>(buf), kBoomMaxLen,
        "$AIRDAQ,64.12.B8,ADC,"     // 21 chars
        "9842,8152,3942,4006,");    // 4 cruise-ish ADC counts + trailing ','
    // Parser sums buf[0..n-2] (everything before the separator). The
    // separator ',' is at index n-1. CRC follows as 2 hex chars.
    int crc = 0;
    for (int i = 0; i < n - 1; i++) crc += buf[i];
    crc &= 0xFF;
    char tail[5];
    std::snprintf(tail, sizeof(tail), "%02X\r\n", crc);
    std::memcpy(buf + n, tail, 4);
    return static_cast<std::size_t>(n + 4);
}

}   // namespace

// ===========================================================================
// Cached static instances. Built on first access (Meyers singleton);
// lifetime is process. Thread-safe under C++11 magic statics.
// ===========================================================================

const Frame& Vn300Frame() {
    static uint8_t bytes[kVn300Len];
    static bool initialized = false;
    if (!initialized) { BuildVn300(bytes); initialized = true; }
    static const Frame f = { bytes, kVn300Len };
    return f;
}

const Frame* SkyviewFrames() {
    static uint8_t adahrsBytes[kSkyviewAdahrsLen];
    static uint8_t emsBytes   [kSkyviewEmsLen];
    static bool initialized = false;
    if (!initialized) {
        BuildSkyviewAdahrs(adahrsBytes);
        BuildSkyviewEms   (emsBytes);
        initialized = true;
    }
    static const Frame frames[2] = {
        { adahrsBytes, kSkyviewAdahrsLen },
        { emsBytes,    kSkyviewEmsLen    },
    };
    return frames;
}

const Frame& BoomFrame() {
    static uint8_t bytes[kBoomMaxLen];
    static std::size_t len = 0;
    static bool initialized = false;
    if (!initialized) { len = BuildBoom(bytes); initialized = true; }
    static const Frame f = { bytes, len };
    return f;
}

}   // namespace onspeed::test_frames
