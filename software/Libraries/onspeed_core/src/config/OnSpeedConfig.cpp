// OnSpeedConfig.cpp
//
// Implements LoadDefaults() and SuFlaps::SetpointOrderError() for
// OnSpeedConfig.  See OnSpeedConfig.h for the public interface.

#include <config/OnSpeedConfig.h>

#include <cstdio>
#include <string>

namespace onspeed::config {

// ----------------------------------------------------------------------------
// SuFlaps::SetpointOrderError
// ----------------------------------------------------------------------------

static std::string fmt1(float f) {
    // Match Arduino's `String(f, 1)` — one decimal place.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", f);
    return std::string(buf);
}

std::string OnSpeedConfig::SuFlaps::SetpointOrderError() const
{
    std::string sErr;
    if (fLDMAXAOA >= fONSPEEDFASTAOA)
        sErr += "LDMAX (" + fmt1(fLDMAXAOA) + ") must be less than OnSpeedFast (" + fmt1(fONSPEEDFASTAOA) + "); ";
    if (fONSPEEDFASTAOA >= fONSPEEDSLOWAOA)
        sErr += "OnSpeedFast (" + fmt1(fONSPEEDFASTAOA) + ") must be less than OnSpeedSlow (" + fmt1(fONSPEEDSLOWAOA) + "); ";
    if (fONSPEEDSLOWAOA >= fSTALLWARNAOA)
        sErr += "OnSpeedSlow (" + fmt1(fONSPEEDSLOWAOA) + ") must be less than StallWarn (" + fmt1(fSTALLWARNAOA) + "); ";
    if (fSTALLAOA != 0.0f && fSTALLWARNAOA >= fSTALLAOA)
        sErr += "StallWarn (" + fmt1(fSTALLWARNAOA) + ") must be less than Stall (" + fmt1(fSTALLAOA) + "); ";
    // Trim trailing "; "
    if (sErr.length() > 2)
        sErr.erase(sErr.length() - 2);
    return sErr;
}

// ----------------------------------------------------------------------------
// LoadDefaults
// ----------------------------------------------------------------------------

bool OnSpeedConfig::LoadDefaults()
{
    // ALL config items should be initialised to reasonable values here.

    iAoaSmoothing       = 20;
    iPressureSmoothing  = 15;
    iMuteAudioUnderIAS  = 30;

    suDataSrc.enSrc     = SuDataSource::EnSensors;

    sReplayLogFileName.clear();

    // Flap positions.
    //
    // Push a single zeroed entry so sketch-side consumers (Audio.cpp,
    // SensorIO.cpp, DisplaySerial.cpp, DataServer.cpp, LogReplay.cpp)
    // can safely dereference aFlaps[g_Flaps.iIndex] on a fresh
    // device. Per-aircraft calibration values stay zero here — the
    // compiled-in defaults must not ship with one airplane's
    // calibration baked in.
    //
    // Zero is the explicit "uncalibrated" signal the audio-tone gate
    // in ToneCalc looks for (fSTALLWARNAOA <= 0 → silent), and the
    // web UI's SetpointOrderError() surfaces the same state as an
    // ordering violation so the user knows calibration is needed.
    //
    // The polynomial curve type (iCurveType = 1) is a structural
    // default, not calibration data: the curve evaluator branches on
    // it. With zero coefficients the polynomial evaluates to 0 AOA
    // for every pressure ratio — which, combined with the audio
    // gate, means "nothing happens until the user calibrates."
    aFlaps.clear();
    SuFlaps suFlaps;
    suFlaps.AoaCurve.iCurveType = 1;
    aFlaps.push_back(suFlaps);

    // Volume
    bVolumeControl      = false;
    iVolumeHighAnalog   = 4095;
    iVolumeLowAnalog    =    0;
    iDefaultVolume      =  100;

    bAudio3D            = false;
    bOverGWarning       = false;

    // CAS curve
    CasCurve.iCurveType = 1;
    CasCurve.afCoeff[0] = 0.0f;  // x^3
    CasCurve.afCoeff[1] = 0.0f;  // x^2
    CasCurve.afCoeff[2] = 1.0f;  // x^1
    CasCurve.afCoeff[3] = 0.0f;  // x^0
    bCasCurveEnabled    = false;

    sPortsOrientation   = "FORWARD";
    sBoxtopOrientation  = "UP";

    // Calibration data source
    sCalSource          = "ONSPEED";
    bCalSourceEfis      = false;

    // Biases
    iPFwdBias           = 8192;
    iP45Bias            = 8192;
    fPStaticBias        = 0.0f;
    fGxBias             = 0.0f;
    fGyBias             = 0.0f;
    fGzBias             = 0.0f;
    fPitchBias          = 0.0f;
    fRollBias           = 0.0f;

    // AHRS algorithm: 0=Madgwick (default), 1=EKF6
    iAhrsAlgorithm      = 0;

    // Serial inputs
    bReadBoom           = false;
    bReadEfisData       = false;
    sEfisType           = "VN-300";

    // Hardware feature toggles
    bOatSensor          = false;
    bBoomChecksum       = true;

    // Serial output
    sSerialOutFormat    = "ONSPEED";
    enSerialOutFormat   = EnSerialFmtOnSpeed;

    // Load limit
    fLoadLimitPositive  =  4.0f;
    fLoadLimitNegative  = -2.0f;

    fAsymmetricGyroLimit = 15.0f;
    fAsymmetricReduction = 2.0f / 3.0f;   // 0.666...

    bBoomConvertData     = false;

    iLogRate             = 50;

    // Vno chime
    iVno                = 150;
    uVnoChimeInterval   = 3;
    bVnoChimeEnabled    = false;

    // SD card logging
    bSdLogging          = false;

    // Aircraft parameters
    iAcGrossWeight      = 0;
    fAcBestGlideIAS     = 0.0f;
    fAcVfe              = 0.0f;
    fAcGlimit           = 0.0f;

    return true;
}

// ----------------------------------------------------------------------------
// EnsureAtLeastOneFlap
// ----------------------------------------------------------------------------

bool EnsureAtLeastOneFlap(std::vector<OnSpeedConfig::SuFlaps>& aFlaps)
{
    if (!aFlaps.empty()) return false;

    OnSpeedConfig::SuFlaps suFlaps;     // default ctor zeros everything
    suFlaps.AoaCurve.iCurveType = 1;    // polynomial — match LoadDefaults
    aFlaps.push_back(suFlaps);
    return true;
}

}  // namespace onspeed::config
