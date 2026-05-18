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
    // Alpha0 is the body angle at zero wing lift — typically negative
    // (most airframes have positive wing incidence). The percent-lift
    // formula `(BodyAngle − alpha_0) / (alpha_stall − alpha_0)` requires
    // alpha_0 to sit on the no-lift side of every setpoint; in practice
    // the binding constraint is `alpha_0 < LDMAX` (the slowest setpoint
    // on the lifting side). A user typo that flips the sign of alpha_0
    // or types the wrong magnitude would otherwise invert or distort
    // the percent-lift bar without any visible warning.
    //
    // Skip when alpha_0 is exactly zero — that's the uncalibrated
    // default value, same convention as fSTALLAOA above.
    if (fAlpha0 != 0.0f && fAlpha0 >= fLDMAXAOA)
        sErr += "Alpha0 (" + fmt1(fAlpha0) + ") must be less than LDMAX (" + fmt1(fLDMAXAOA) + "); ";
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

    // AHRS algorithm: 0=Madgwick (default), 1=EKFQ.
    iAhrsAlgorithm      = 0;

    // EKFQ defaults — keep in sync with EKFQ::Config::defaults() in
    // ahrs/EKFQ.cpp. Optuna study ekfq_v15 best trial (cruise-AOA loss);
    // these will be re-locked once the ekfq_v16 study with expanded
    // bounds completes.
    fEkfqQQuat          = 1.3876969636637712e-06f;
    fEkfqQBias          = 0.08059835153457112f;
    fEkfqQZ             = 0.000760923735397084f;
    fEkfqQVz            = 0.00011863168292171215f;
    fEkfqQBaz           = 0.00011126568311539455f;
    fEkfqQBeta          = 3.525093877403027e-08f;
    fEkfqRAx            = 16.594799559856998f;
    fEkfqRAy            = 10.855551467684359f;
    fEkfqRAz            = 12.758742327066178f;
    fEkfqRBaro          = 5.015458731982534f;
    fEkfqRBetaPrior     = 0.13441309019767658f;
    fEkfqRBiasPrior     = 0.00035857919885685886f;
    fEkfqKBetaR         = 6.79749216596058f;
    fEkfqAccelEmaAlpha  = 0.052324843677354384f;
    fEkfqCompFadeTauSec = 2.531734433346506f;
    fEkfqIasAliveKt     = 33.66929039144636f;
    fEkfqTasdotEmaAlpha = 0.20081238948995161f;
    fEkfqTasMinMps      = 12.0f;

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

    // Aircraft parameters.  Defaults match the Utility category radio
    // (+4.4 G / -1.76 G) so a freshly defaulted config selects a sane,
    // labeled preset rather than an out-of-band 0.0 G that the UI shows
    // as Custom with an empty negative field.
    iAcGrossWeight      = 0;
    fAcBestGlideIAS     = 0.0f;
    fAcVfe              = 0.0f;
    fAcGlimit           =  4.4f;
    fAcNegGlimit        = -1.76f;
    // Custom storage seeds match the Utility default — sane starting
    // point if the pilot picks Custom for the first time and edits.
    fCustomAcGlimit     =  4.4f;
    fCustomAcNegGlimit  = -1.76f;

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
