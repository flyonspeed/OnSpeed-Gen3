// Vn300.cpp
//
// Ported verbatim from EfisSerial.cpp (EnVN300 branch). No algorithmic
// changes — characterisation first.
//
// NOTE: The original firmware uses millis() for the fractional second in
// szTimeUTC. This parser cannot call millis() (onspeed_core must be
// platform-free). The szTimeUTC field is populated with whole seconds only;
// consumers that need sub-second precision must supply their own timestamp.

#include <efis/Vn300.h>

#include <cstring>
#include <cstdio>

namespace onspeed::efis {

// ---------------------------------------------------------------------------
// array2float / array2double helpers (verbatim from EfisSerial.cpp)
// ---------------------------------------------------------------------------

static float array2float(const uint8_t* buffer, int startIndex)
{
    float out;
    memcpy(&out, buffer + startIndex, sizeof(float));
    return out;
}

static double array2double(const uint8_t* buffer, int startIndex)
{
    double out;
    memcpy(&out, buffer + startIndex, sizeof(double));
    return out;
}

// ---------------------------------------------------------------------------
// Vn300Parser
// ---------------------------------------------------------------------------

void Vn300Parser::FeedByte(uint8_t b)
{
    // Sync detection: 0xFA followed by 0x19.
    if (b == 0x19 && prevByte_ == 0xFA)
    {
        inProgress_ = true;
        buf_[0]  = 0xFA;
        buf_[1]  = 0x19;
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
        // Validate header.
        const uint8_t kHeader[8] = {0xFA, 0x19, 0xE0, 0x01, 0x91, 0x00, 0x42, 0x01};
        if (memcmp(buf_, kHeader, 8) != 0)
        {
            inProgress_ = false;
            prevByte_   = b;
            return;
        }

        // CRC-16 check (VN proprietary): computed over bytes 1..(N-2).
        // A valid packet gives vnCrc == 0 when the two CRC bytes are
        // included in the computation.
        uint16_t vnCrc = 0;
        for (int i = 1; i < kPacketSize; i++)
        {
            vnCrc  = static_cast<uint16_t>((vnCrc >> 8) | (vnCrc << 8));
            vnCrc ^= static_cast<uint8_t>(buf_[i]);
            vnCrc ^= static_cast<uint16_t>(static_cast<uint8_t>(vnCrc & 0xFF) >> 4);
            vnCrc ^= static_cast<uint16_t>((vnCrc << 8) << 4);
            vnCrc ^= static_cast<uint16_t>(((vnCrc & 0xFF) << 4) << 1);
        }

        if (vnCrc != 0)
        {
            inProgress_ = false;
            prevByte_   = b;
            return;
        }

        Decode();
        inProgress_ = false;
    }

    prevByte_ = b;
}

std::optional<EfisFrame> Vn300Parser::TakeFrame()
{
    if (!pendingFrame_)
        return std::nullopt;
    EfisFrame out = *pendingFrame_;
    pendingFrame_.reset();
    return out;
}

std::optional<Vn300Data> Vn300Parser::TakeVn300Data()
{
    if (!pendingData_)
        return std::nullopt;
    Vn300Data out = *pendingData_;
    pendingData_.reset();
    return out;
}

void Vn300Parser::Reset()
{
    bufLen_     = 0;
    inProgress_ = false;
    prevByte_   = 0;
    pendingFrame_.reset();
    pendingData_.reset();
}

void Vn300Parser::Decode()
{
    Vn300Data data;

    data.angularRateRoll  = array2float(buf_,  8);
    data.angularRatePitch = array2float(buf_, 12);
    data.angularRateYaw   = array2float(buf_, 16);
    data.gnssLat          = array2double(buf_, 20);
    data.gnssLon          = array2double(buf_, 28);
    data.estAltMeters     = array2double(buf_, 36);

    data.velNedNorth      = array2float(buf_, 44);
    data.velNedEast       = array2float(buf_, 48);
    data.velNedDown       = array2float(buf_, 52);

    data.accelFwd         = array2float(buf_, 56);
    data.accelLat         = array2float(buf_, 60);
    data.accelVert        = array2float(buf_, 64);

    // GPS time
    uint8_t vnHour = buf_[71];
    uint8_t vnMin  = buf_[72];
    uint8_t vnSec  = buf_[73];
    // NOTE: fractional seconds from millis() are omitted here (not
    // platform-free). Consumers needing sub-second resolution must augment.
    snprintf(data.szTimeUTC, sizeof(data.szTimeUTC),
             "%u:%u:%u", vnHour, vnMin, vnSec);

    data.gpsFix            = buf_[76];
    data.gnssVelNedNorth   = array2float(buf_, 77);
    data.gnssVelNedEast    = array2float(buf_, 81);
    data.gnssVelNedDown    = array2float(buf_, 85);

    data.yaw               = array2float(buf_, 89);
    data.pitch             = array2float(buf_, 93);
    data.roll              = array2float(buf_, 97);

    data.linAccFwd         = array2float(buf_, 101);
    data.linAccLat         = array2float(buf_, 105);
    data.linAccVert        = array2float(buf_, 109);

    data.yawSigma          = array2float(buf_, 113);
    data.pitchSigma        = array2float(buf_, 117);   // YprU.c1 = pitch per UM005 §5.8.8
    data.rollSigma         = array2float(buf_, 121);   // YprU.c2 = roll

    pendingData_ = data;

    // Populate normalised EfisFrame from the VN-300 attitude fields. Only
    // attitude is written; IAS/TAS/Palt/VSI/OAT stay at kEfisFieldAbsent
    // (NaN) so applyFrame() will hold any values an on-board sensor or
    // another EFIS source populated earlier.
    EfisFrame frame;
    frame.pitchDeg   = data.pitch;
    frame.rollDeg    = data.roll;
    frame.headingDeg = data.yaw;
    frame.source     = EfisSource::Vn300;
    pendingFrame_    = frame;
}

}   // namespace onspeed::efis
