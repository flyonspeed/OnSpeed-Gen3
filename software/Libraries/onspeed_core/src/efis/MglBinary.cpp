// MglBinary.cpp
//
// Ported verbatim from EfisSerial.cpp (EnMglBinary branch). No algorithmic
// changes — characterisation first.
//
// The original firmware accesses the buffer via the MGL/MGL_Msg1/MGL_Msg3
// packed structs using a cast. We reproduce that exact memory layout here
// using the same pragma pack(push,1) structs, because the original code
// relies on endian-native struct member access which must be preserved.

#include <efis/MglBinary.h>

#include <cstring>
#include <cstdio>

namespace onspeed::efis {

// ---------------------------------------------------------------------------
// MGL packed structs (verbatim from EfisSerial.cpp)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct MglMsg1
{
    int32_t  PAltitude;
    int32_t  BAltitude;
    uint16_t IAS;
    uint16_t TAS;
    int16_t  AOA;
    int16_t  VSI;
    uint16_t Baro;
    uint16_t Local;
    int16_t  OAT;
    uint8_t  Humidity;
    uint8_t  SystemFlags;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Date;
    uint8_t  Month;
    uint8_t  Year;
    uint8_t  FTHour;
    uint8_t  FTMin;
    int32_t  Checksum;
};

struct MglMsg3
{
    uint16_t HeadingMag;
    int16_t  PitchAngle;
    int16_t  BankAngle;
    int16_t  YawAngle;
    int16_t  TurnRate;
    int16_t  Slip;
    int16_t  GForce;
    int16_t  LRForce;
    int16_t  FRForce;
    int16_t  BankRate;
    int16_t  PitchRate;
    int16_t  YawRate;
    uint8_t  SensorFlags;
    uint8_t  PaddingByte1;
    uint8_t  PaddingByte2;
    uint8_t  PaddingByte3;
    int32_t  Checksum;
};

struct MglHeader
{
    uint8_t  DLE;
    uint8_t  STX;
    uint8_t  MessageLength;
    uint8_t  MessageLengthXOR;
    uint8_t  MessageType;
    uint8_t  MessageRate;
    uint8_t  MessageCount;
    uint8_t  MessageVersion;
    union {
        MglMsg1 Msg1;
        MglMsg3 Msg3;
    };
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// MglBinaryParser
// ---------------------------------------------------------------------------

void MglBinaryParser::FeedByte(uint8_t b)
{
    // Sync byte 1
    if (bufLen_ == 0)
    {
        if (b == 0x05)
        {
            buf_[0] = b;
            bufLen_  = 1;
        }
        return;
    }

    // Sync byte 2
    if (bufLen_ == 1)
    {
        if (b == 0x02)
        {
            buf_[1] = b;
            bufLen_  = 2;
        }
        else
        {
            bufLen_ = 0;
        }
        return;
    }

    // Length bytes (positions 2 and 3)
    if (bufLen_ == 2 || bufLen_ == 3)
    {
        buf_[bufLen_++] = b;
        if (bufLen_ == 4)
        {
            // Validate length XOR: MessageLength ^ MessageLengthXOR must be 0xFF
            if ((buf_[2] ^ buf_[3]) != 0xFF)
            {
                bufLen_ = 0;
                return;
            }
            // Compute target message length: MessageLength + 20
            // (MessageLength of 0x00 is treated as 256)
            int ml = buf_[2];
            if (ml == 0x00)
                ml = 256;
            msgLen_ = ml + 20;
        }
        return;
    }

    // Body bytes
    if (bufLen_ > 3 && bufLen_ < msgLen_)
    {
        if (bufLen_ < static_cast<int>(kBufSize))
            buf_[bufLen_] = b;
        bufLen_++;
    }

    // Complete message
    if (bufLen_ > 3 && bufLen_ >= msgLen_)
    {
        Decode();
        bufLen_ = 0;
    }
}

bool MglBinaryParser::TryTakeFrame(EfisFrame& out)
{
    if (!pendingReady_) return false;
    out = pending_;
    pendingReady_ = false;
    return true;
}

std::optional<EfisFrame> MglBinaryParser::TakeFrame()
{
    if (!pendingReady_) return std::nullopt;
    EfisFrame out = pending_;
    pendingReady_ = false;
    return out;
}

void MglBinaryParser::Reset()
{
    bufLen_       = 0;
    msgLen_       = 0;
    pendingReady_ = false;
}

void MglBinaryParser::Decode()
{
    // Overlay the packed struct onto the raw buffer (verbatim from original).
    const MglHeader* msg = reinterpret_cast<const MglHeader*>(buf_);

    switch (msg->MessageType)
    {
        case 1:   // Primary flight data
        {
            if (bufLen_ != 44)
                break;

            EfisFrame out;
            // IAS in 10th of km/h → knots: × 0.05399565
            out.iasKt      = msg->Msg1.IAS * 0.05399565f;
            out.tasKt      = msg->Msg1.TAS * 0.05399565f;
            out.paltFt     = static_cast<float>(msg->Msg1.PAltitude);
            out.aoaPercent = static_cast<float>(msg->Msg1.AOA);
            out.vsiFpm     = static_cast<float>(msg->Msg1.VSI);
            out.oatCelsius = static_cast<float>(msg->Msg1.OAT);
            out.source     = EfisSource::Mgl;
            out.fieldsPresent |=
                onspeed::EfisField::Ias        |
                onspeed::EfisField::Tas        |
                onspeed::EfisField::Palt       |
                onspeed::EfisField::AoaPercent |
                onspeed::EfisField::Vsi        |
                onspeed::EfisField::OatCelsius;
            // Time-of-day from packed Msg1 fields. MGL is binary so no
            // sentinel-byte gating; clamp the values to the valid HH:MM:SS
            // range as a defense against a corrupt or never-set RTC.
            if (msg->Msg1.Hour <= 23 && msg->Msg1.Minute <= 59 && msg->Msg1.Second <= 59)
                snprintf(out.timeOfDayHms, sizeof(out.timeOfDayHms),
                         "%02u:%02u:%02u",
                         msg->Msg1.Hour, msg->Msg1.Minute, msg->Msg1.Second);
            pending_      = out;
            pendingReady_ = true;
            break;
        }

        case 3:   // Attitude data
        {
            if (bufLen_ != 40)
                break;

            EfisFrame out;
            out.headingDeg = static_cast<float>(msg->Msg3.HeadingMag) * 0.1f;
            out.pitchDeg   = msg->Msg3.PitchAngle * 0.1f;
            out.rollDeg    = msg->Msg3.BankAngle  * 0.1f;
            out.verticalG  = msg->Msg3.GForce     * 0.01f;
            out.lateralG   = msg->Msg3.LRForce    * 0.01f;
            out.source     = EfisSource::Mgl;
            out.fieldsPresent |=
                onspeed::EfisField::Heading   |
                onspeed::EfisField::Pitch     |
                onspeed::EfisField::Roll      |
                onspeed::EfisField::VerticalG |
                onspeed::EfisField::LateralG;
            pending_      = out;
            pendingReady_ = true;
            break;
        }

        default:
            break;
    }
}

}   // namespace onspeed::efis
