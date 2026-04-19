
#include "Globals.h"
#include "src/io/EfisSerialPort.h"

// ---------------------------------------------------------------------------
// EfisSerialPort
// ---------------------------------------------------------------------------

EfisSerialPort::EfisSerialPort()
    : pSerial(nullptr)
    , enType(EnNone)
    , uTimestamp(0)
    , lastReceivedEfisTime(0)
    , parser_(onspeed::efis::EfisType::None)
{
    suEfis.DecelRate        = 0.00f;
    suEfis.IAS              = 0.00f;
    suEfis.Pitch            = 0.00f;
    suEfis.Roll             = 0.00f;
    suEfis.LateralG         = 0.00f;
    suEfis.VerticalG        = 0.00f;
    suEfis.PercentLift      = 0;
    suEfis.Palt             = 0;
    suEfis.VSI              = 0;
    suEfis.TAS              = 0.00f;
    suEfis.OAT              = 0.00f;
    suEfis.FuelRemaining    = 0.00f;
    suEfis.FuelFlow         = 0.00f;
    suEfis.MAP              = 0.00f;
    suEfis.RPM              = 0;
    suEfis.PercentPower     = 0;
    suEfis.Heading          = -1;
    suEfis.szTime[0]        = '\0';

    suVN300.AngularRateRoll  = 0.00f;
    suVN300.AngularRatePitch = 0.00f;
    suVN300.AngularRateYaw   = 0.00f;
    suVN300.VelNedNorth      = 0.00f;
    suVN300.VelNedEast       = 0.00f;
    suVN300.VelNedDown       = 0.00f;
    suVN300.AccelFwd         = 0.00f;
    suVN300.AccelLat         = 0.00f;
    suVN300.AccelVert        = 0.00f;
    suVN300.Yaw              = 0.00f;
    suVN300.Pitch            = 0.00f;
    suVN300.Roll             = 0.00f;
    suVN300.LinAccFwd        = 0.00f;
    suVN300.LinAccLat        = 0.00f;
    suVN300.LinAccVert       = 0.00f;
    suVN300.YawSigma         = 0.00f;
    suVN300.RollSigma        = 0.00f;
    suVN300.PitchSigma       = 0.00f;
    suVN300.GnssVelNedNorth  = 0.00f;
    suVN300.GnssVelNedEast   = 0.00f;
    suVN300.GnssVelNedDown   = 0.00f;
    suVN300.GPSFix           = 0;
    suVN300.GnssLat          = 0.00;
    suVN300.GnssLon          = 0.00;
    suVN300.szTimeUTC[0]     = '\0';

    uTimestamp = millis();
}

// ---------------------------------------------------------------------------

onspeed::efis::EfisType EfisSerialPort::toCoreType(EnEfisType t)
{
    switch (t)
    {
        case EnVN300:        return onspeed::efis::EfisType::Vn300;
        case EnDynonSkyview: return onspeed::efis::EfisType::DynonSkyview;
        case EnDynonD10:     return onspeed::efis::EfisType::DynonD10;
        case EnGarminG5:     return onspeed::efis::EfisType::GarminG5;
        case EnGarminG3X:    return onspeed::efis::EfisType::GarminG3X;
        case EnMglBinary:    return onspeed::efis::EfisType::MglBinary;
        case EnNone:
        default:
            return onspeed::efis::EfisType::None;
    }
}

// ---------------------------------------------------------------------------

void EfisSerialPort::Init(EnEfisType enEfisType, HardwareSerial* pEfisSerial)
{
    uint32_t hwSerialConfig = SerialConfig::SERIAL_8N1;

    enType  = enEfisType;
    pSerial = pEfisSerial;

    parser_.ChangeType(toCoreType(enType));

    pSerial->end();

    if (enType != EnNone)
    {
        pSerial->begin(115200, hwSerialConfig, EFIS_SER_RX, EFIS_SER_TX, false);
    }
}

// ---------------------------------------------------------------------------

void EfisSerialPort::Read()
{
    if (!g_Config.bReadEfisData)
        return;

    static constexpr int kPacketSize = 512;
    int packetCount = 0;

    while (pSerial->available() && packetCount < kPacketSize)
    {
        uint8_t b = static_cast<uint8_t>(pSerial->read());
        lastReceivedEfisTime = millis();
        packetCount++;

        parser_.FeedByte(b);

        if (auto frame = parser_.TakeFrame())
        {
            applyFrame(*frame);
            uTimestamp = millis();
        }

        if (enType == EnVN300)
        {
            if (auto data = parser_.TakeVn300Data())
            {
                applyVn300Data(*data);
            }
        }
    }
}

// ---------------------------------------------------------------------------

void EfisSerialPort::applyFrame(const onspeed::EfisFrame& frame)
{
    using onspeed::EfisSource;

    // Sentinel -1 values from the parser mean "field not supported / not
    // available in this frame". We only update suEfis when the value is
    // valid (not the sentinel).

    if (frame.iasKt >= 0.0f)         suEfis.IAS         = frame.iasKt;
    if (frame.pitchDeg > -100.0f)    suEfis.Pitch       = frame.pitchDeg;
    if (frame.rollDeg  > -180.0f)    suEfis.Roll        = frame.rollDeg;
    if (frame.headingDeg >= 0.0f)    suEfis.Heading     = static_cast<int>(frame.headingDeg);
    if (frame.lateralG  > -100.0f)   suEfis.LateralG    = frame.lateralG;
    if (frame.verticalG > -100.0f)   suEfis.VerticalG   = frame.verticalG;
    if (frame.aoaPercent >= 0.0f)    suEfis.PercentLift = static_cast<int>(frame.aoaPercent);
    if (frame.paltFt > -10000.0f)    suEfis.Palt        = static_cast<int>(frame.paltFt);
    if (frame.vsiFpm > -10000.0f)    suEfis.VSI         = static_cast<int>(frame.vsiFpm);
    if (frame.tasKt >= 0.0f)         suEfis.TAS         = frame.tasKt;
    if (frame.oatCelsius > -100.0f)  suEfis.OAT         = frame.oatCelsius;

    // VN-300: also update suVN300 attitude fields from the normalised frame.
    if (frame.source == EfisSource::Vn300)
    {
        suVN300.Pitch = frame.pitchDeg;
        suVN300.Roll  = frame.rollDeg;
        suVN300.Yaw   = frame.headingDeg;
    }
}

// ---------------------------------------------------------------------------

void EfisSerialPort::applyVn300Data(const onspeed::efis::Vn300Data& data)
{
    suVN300.AngularRateRoll  = data.angularRateRoll;
    suVN300.AngularRatePitch = data.angularRatePitch;
    suVN300.AngularRateYaw   = data.angularRateYaw;
    suVN300.VelNedNorth      = data.velNedNorth;
    suVN300.VelNedEast       = data.velNedEast;
    suVN300.VelNedDown       = data.velNedDown;
    suVN300.AccelFwd         = data.accelFwd;
    suVN300.AccelLat         = data.accelLat;
    suVN300.AccelVert        = data.accelVert;
    suVN300.Yaw              = data.yaw;
    suVN300.Pitch            = data.pitch;
    suVN300.Roll             = data.roll;
    suVN300.LinAccFwd        = data.linAccFwd;
    suVN300.LinAccLat        = data.linAccLat;
    suVN300.LinAccVert       = data.linAccVert;
    suVN300.YawSigma         = data.yawSigma;
    suVN300.RollSigma        = data.rollSigma;
    suVN300.PitchSigma       = data.pitchSigma;
    suVN300.GnssVelNedNorth  = data.gnssVelNedNorth;
    suVN300.GnssVelNedEast   = data.gnssVelNedEast;
    suVN300.GnssVelNedDown   = data.gnssVelNedDown;
    suVN300.GnssLat          = data.gnssLat;
    suVN300.GnssLon          = data.gnssLon;
    suVN300.GPSFix           = data.gpsFix;

    // Copy the time string
    strncpy(suVN300.szTimeUTC, data.szTimeUTC, sizeof(suVN300.szTimeUTC) - 1);
    suVN300.szTimeUTC[sizeof(suVN300.szTimeUTC) - 1] = '\0';

    // Append the fractional seconds computed from millis() (original firmware
    // behaviour; this is the one place millis() is needed for VN-300 time).
    int iFrac = static_cast<int>(millis() / 10) % 100;
    // Find where the null terminator is and append ".XX".
    size_t len = strnlen(suVN300.szTimeUTC, sizeof(suVN300.szTimeUTC) - 4);
    snprintf(suVN300.szTimeUTC + len, sizeof(suVN300.szTimeUTC) - len,
             ".%02d", iFrac);
}
