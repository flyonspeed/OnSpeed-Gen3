// Vn300.cpp
//
// VectorNav VN-300 binary parser. Hot-path optimizations:
//   - 256-entry CRC-16 lookup table (~3x faster than the byte-wise XOR/shift).
//   - Copy-free pending-frame: TryTakeFrame() / TryTakeVn300Data() write
//     directly into caller-supplied storage with one bool check.
//   - Presence bitmask on the normalised EfisFrame so consumers can branch
//     on integer bits instead of std::isfinite() per float.
//
// Per-sample timestamps (TimeStartup, TimeGps) come from the Common group
// at IMU sample time; the deprecated GNSS1.UTC field has been dropped.

#include <efis/Vn300.h>

#include <cstring>

namespace onspeed::efis {

// ---------------------------------------------------------------------------
// CRC-16 lookup table for the VectorNav proprietary CRC.
//
// The byte-wise algorithm (from the firmware shipped with VN-300):
//
//   crc = (crc >> 8) | (crc << 8);
//   crc ^= byte;
//   crc ^= (crc & 0xFF) >> 4;
//   crc ^= (crc << 8) << 4;
//   crc ^= ((crc & 0xFF) << 4) << 1;
//
// We unroll this into a 256-entry table such that, for every input byte b
// applied to a crc seed (hi<<8 | lo):
//
//   new_crc = (lo << 8) ^ table[hi ^ b]
//
// where table[i] is the result of the four XOR/shift steps applied to a
// starting crc whose hi-byte is i and lo-byte is 0. This matches the
// standard table-driven CRC-CCITT pattern; correctness is asserted by
// the parity test that compares table-driven and byte-wise CRC over a
// fuzz corpus (see test_efis_vn300 / parity tests).
// ---------------------------------------------------------------------------
static constexpr uint16_t computeCrc16Entry(uint8_t i)
{
    uint16_t crc = static_cast<uint16_t>(i) << 8;
    crc = static_cast<uint16_t>((crc >> 8) | (crc << 8));
    crc ^= static_cast<uint16_t>(static_cast<uint8_t>(crc & 0xFF) >> 4);
    crc ^= static_cast<uint16_t>((crc << 8) << 4);
    crc ^= static_cast<uint16_t>(((crc & 0xFF) << 4) << 1);
    return crc;
}

static constexpr auto kCrc16Table = []() {
    struct T { uint16_t v[256]; };
    T t{};
    for (int i = 0; i < 256; i++)
        t.v[i] = computeCrc16Entry(static_cast<uint8_t>(i));
    return t;
}();

// Table-driven CRC over bytes [start, end). Returns 0 on a valid packet
// (CRC bytes included), matching the byte-wise algorithm.
static inline uint16_t vnCrc16(const uint8_t* buf, int start, int end)
{
    uint16_t crc = 0;
    for (int i = start; i < end; i++)
    {
        const uint8_t hi  = static_cast<uint8_t>(crc >> 8);
        const uint8_t lo  = static_cast<uint8_t>(crc & 0xFF);
        const uint8_t idx = static_cast<uint8_t>(hi ^ buf[i]);
        crc = static_cast<uint16_t>(
                  static_cast<uint16_t>(lo) << 8) ^ kCrc16Table.v[idx];
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Little-endian decoders. Force-inlined so they don't survive as call
// boundaries through static-function ABI.
// ---------------------------------------------------------------------------
__attribute__((always_inline))
static inline float array2float(const uint8_t* buffer, int startIndex)
{
    float out;
    memcpy(&out, buffer + startIndex, sizeof(float));
    return out;
}

__attribute__((always_inline))
static inline double array2double(const uint8_t* buffer, int startIndex)
{
    double out;
    memcpy(&out, buffer + startIndex, sizeof(double));
    return out;
}

__attribute__((always_inline))
static inline uint64_t array2u64(const uint8_t* buffer, int startIndex)
{
    uint64_t out;
    memcpy(&out, buffer + startIndex, sizeof(uint64_t));
    return out;
}

// ---------------------------------------------------------------------------
// Vn300Parser
// ---------------------------------------------------------------------------

void Vn300Parser::FeedByte(uint8_t b)
{
    diag_.bytesFed++;
    // Track what bytes are arriving when we're NOT mid-frame. If a real
    // VN-300 is talking, this distribution should be dominated by 0xFA
    // (start of every frame, ~1 in 138 bytes is 0xFA, rest are payload).
    // If we're seeing line noise, the distribution will be more uniform.
    if (!inProgress_)
        diag_.firstByteByNibble[(b >> 4) & 0xF]++;
    if (b == 0xFA)
        diag_.sync1++;

    // Sync detection: VectorNav sync byte 0xFA followed by the Groups
    // byte 0x1B (Common+Time+GNSS1+AHRS). For the old 3-group config
    // the Groups byte was 0x19; that frame format is no longer
    // supported (fail-closed via header memcmp below).
    if (b == 0x1B && prevByte_ == 0xFA)
    {
        diag_.sync2++;
        inProgress_ = true;
        buf_[0]  = 0xFA;
        buf_[1]  = 0x1B;
        bufLen_  = 2;
        prevByte_ = b;
        return;
    }

    if (bufLen_ < kPacketSize && inProgress_)
    {
        buf_[bufLen_++] = b;
    }

    if (bufLen_ == kPacketSize && inProgress_)
    {
        // Validate 10-byte header: sync(1) + Groups(1) + 4 group masks(8).
        static constexpr uint8_t kHeader[10] = {
            0xFA, 0x1B,           // sync + Groups (Common+Time+GNSS1+AHRS)
            0xE3, 0x01,           // Common mask 0x01E3 (LE)
            0x00, 0x02,           // Time mask   0x0200 (LE)
            0x90, 0x00,           // GNSS1 mask  0x0090 (LE)
            0x42, 0x01};          // AHRS mask   0x0142 (LE)
        static_assert(sizeof(kHeader) == 10,
                      "kHeader must be 10 bytes "
                      "(sync 1 + Groups 1 + 4 group masks * 2)");
        if (memcmp(buf_, kHeader, sizeof(kHeader)) != 0)
        {
            diag_.headerFail++;
            inProgress_ = false;
            prevByte_   = b;
            return;
        }
        diag_.headerOk++;

        // Table-driven CRC over bytes 1..(N-1) inclusive; valid packet yields 0.
        const uint16_t crc = vnCrc16(buf_, 1, kPacketSize);
        if (crc != 0)
        {
            diag_.crcFail++;
            inProgress_ = false;
            prevByte_   = b;
            return;
        }
        diag_.crcOk++;

        Decode();
        inProgress_ = false;
    }

    prevByte_ = b;
}

bool Vn300Parser::TryTakeFrame(EfisFrame& out)
{
    if (!pendingFrameReady_) return false;
    out = pendingFrame_;
    pendingFrameReady_ = false;
    return true;
}

bool Vn300Parser::TryTakeVn300Data(Vn300Data& out)
{
    if (!pendingDataReady_) return false;
    out = pendingData_;
    pendingDataReady_ = false;
    return true;
}

std::optional<EfisFrame> Vn300Parser::TakeFrame()
{
    if (!pendingFrameReady_) return std::nullopt;
    EfisFrame out = pendingFrame_;
    pendingFrameReady_ = false;
    return out;
}

std::optional<Vn300Data> Vn300Parser::TakeVn300Data()
{
    if (!pendingDataReady_) return std::nullopt;
    Vn300Data out = pendingData_;
    pendingDataReady_ = false;
    return out;
}

void Vn300Parser::Reset()
{
    bufLen_            = 0;
    inProgress_        = false;
    prevByte_          = 0;
    pendingFrameReady_ = false;
    pendingDataReady_  = false;
}

void Vn300Parser::Decode()
{
    Vn300Data data;

    data.timeStartupNs    = array2u64(buf_,    10);
    data.timeGpsNs        = array2u64(buf_,    18);

    data.angularRateRoll  = array2float(buf_,  26);
    data.angularRatePitch = array2float(buf_,  30);
    data.angularRateYaw   = array2float(buf_,  34);

    data.gnssLat          = array2double(buf_, 38);
    data.gnssLon          = array2double(buf_, 46);
    data.estAltMeters     = array2double(buf_, 54);

    data.velNedNorth      = array2float(buf_,  62);
    data.velNedEast       = array2float(buf_,  66);
    data.velNedDown       = array2float(buf_,  70);

    data.accelFwd         = array2float(buf_,  74);
    data.accelLat         = array2float(buf_,  78);
    data.accelVert        = array2float(buf_,  82);

    data.timeStatus       = buf_[86];
    data.gpsFix           = buf_[87];

    data.gnssVelNedNorth  = array2float(buf_,  88);
    data.gnssVelNedEast   = array2float(buf_,  92);
    data.gnssVelNedDown   = array2float(buf_,  96);

    data.yaw              = array2float(buf_, 100);
    data.pitch            = array2float(buf_, 104);
    data.roll             = array2float(buf_, 108);

    data.linAccFwd        = array2float(buf_, 112);
    data.linAccLat        = array2float(buf_, 116);
    data.linAccVert       = array2float(buf_, 120);

    data.yawSigma         = array2float(buf_, 124);
    data.pitchSigma       = array2float(buf_, 128);   // YprU.c1 = pitch per UM005 §5.8.8
    data.rollSigma        = array2float(buf_, 132);   // YprU.c2 = roll

    pendingData_      = data;
    pendingDataReady_ = true;

    // Populate normalised EfisFrame with the VN-300 attitude fields.
    EfisFrame frame;
    frame.pitchDeg   = data.pitch;
    frame.rollDeg    = data.roll;
    frame.headingDeg = data.yaw;
    frame.source     = EfisSource::Vn300;
    frame.fieldsPresent = onspeed::EfisField::Pitch |
                          onspeed::EfisField::Roll  |
                          onspeed::EfisField::Heading;
    pendingFrame_      = frame;
    pendingFrameReady_ = true;
}

}   // namespace onspeed::efis
