// OnSpeedConfig.h
//
// Platform-independent configuration struct for the OnSpeed firmware.
//
// This is the data layer extracted from the sketch's FOSConfig class as part
// of PR 3.1 (Config module extraction).  The struct owns all configuration
// state — flap positions, setpoints, biases, serial/EFIS options — and the
// LoadDefaults() method that initialises it.
//
// The sketch-side `FOSConfig` class (software/OnSpeed-Gen3-ESP32/src/config/
// Config.h) inherits from this type and adds the Arduino/SD-card I/O methods
// (XML parse/emit, file load/save).  Task 2 of PR 3.1 will port the XML
// code into core as well; for now, this header contains only the struct +
// defaults.
//
// Platform-freeness: this header must not include <Arduino.h> or any other
// platform header.  Arduino `String` fields are represented as `std::string`.

#ifndef ONSPEED_CORE_CONFIG_ONSPEED_CONFIG_H
#define ONSPEED_CORE_CONFIG_ONSPEED_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include <util/OnSpeedTypes.h>  // SuCalibrationCurve, MAX_AOA_CURVES, MAX_CURVE_COEFF

namespace onspeed::config {

// ============================================================================
// Fixed-capacity CSV-parsed arrays (V1 config format helpers).
// Kept at file scope — several sketch helpers return these by value.
// V1 is the legacy Gen2-era flat-tag config format; the firmware always
// supports importing it for pilots upgrading from Gen2 SD cards.  See
// ConfigV1Parse.h for the parser; CONFIG2 is the current XML format.
// ============================================================================

struct SuIntArray {
    int Count;
    int Items[onspeed::MAX_AOA_CURVES];
};

struct SuFloatArray {
    int   Count;
    float Items[onspeed::MAX_AOA_CURVES];
};

// ============================================================================
// Data source selector
// ============================================================================

struct SuDataSource {
    enum EnDataSource {
        EnSensors,
        EnReplay,
        EnTestPot,
        EnRangeSweep,
        EnUnknown
    } enSrc;

    const char * toCStr() const {
        return toCStr(enSrc);
    }

    const char * toCStr(EnDataSource enDataSource) const {
        if      (enDataSource == EnDataSource::EnSensors)    return "SENSORS";
        else if (enDataSource == EnDataSource::EnReplay)     return "REPLAYLOGFILE";
        else if (enDataSource == EnDataSource::EnTestPot)    return "TESTPOT";
        else if (enDataSource == EnDataSource::EnRangeSweep) return "RANGESWEEP";
        else if (enDataSource == EnDataSource::EnUnknown)    return "UNKNOWN";
        else                                                 return "UNKNOWN";
    }

    void fromStrSet(const std::string& sDataSource) {
        enSrc = fromStr(sDataSource);
    }

    EnDataSource fromStr(const std::string& sDataSource) {
        if      (sDataSource == "SENSORS")        return EnDataSource::EnSensors;
        else if (sDataSource == "REPLAYLOGFILE")  return EnDataSource::EnReplay;
        else if (sDataSource == "TESTPOT")        return EnDataSource::EnTestPot;
        else if (sDataSource == "RANGESWEEP")     return EnDataSource::EnRangeSweep;
        else                                      return EnDataSource::EnUnknown;
    }
};

// ============================================================================
// OnSpeedConfig — the platform-independent config struct.
// Sketch-side FOSConfig inherits from this and adds Arduino/SD I/O methods.
// ============================================================================

class OnSpeedConfig {
public:
    OnSpeedConfig() { LoadDefaults(); }

    // ------------------------------------------------------------------------
    // Nested types
    // ------------------------------------------------------------------------

    struct SuFlaps {
        SuFlaps()
            : iDegrees(0),
              iPotPosition(0),
              fLDMAXAOA(0.0f),
              fONSPEEDFASTAOA(0.0f),
              fONSPEEDSLOWAOA(0.0f),
              fSTALLWARNAOA(0.0f),
              fSTALLAOA(0.0f),
              fMANAOA(0.0f),
              fAlpha0(0.0f),
              fAlphaStall(0.0f),
              fKFit(0.0f),
              AoaCurve{} {}

        int   iDegrees;
        int   iPotPosition;
        float fLDMAXAOA;
        float fONSPEEDFASTAOA;
        float fONSPEEDSLOWAOA;
        float fSTALLWARNAOA;
        float fSTALLAOA;
        float fMANAOA;
        float fAlpha0;       ///< Zero-lift fuselage AOA (deg), from physics fit
        float fAlphaStall;   ///< Stall AOA from physics fit (deg)
        float fKFit;         ///< Lift sensitivity (deg*kt^2) from IAS-to-AOA fit

        onspeed::SuCalibrationCurve AoaCurve;

        // Returns empty string if AOA setpoints are in order, or a description
        // of all pairs that are out of order.  Skips fSTALLAOA from the chain
        // when it is still at its uncalibrated default (0.0).
        std::string SetpointOrderError() const;
    };

    // Serial-output format cache — avoids string compare in the 10 Hz display
    // loop.  Sketch-side parse assigns this after reading sSerialOutFormat.
    enum EnSerialFmt {
        EnSerialFmtOther,
        EnSerialFmtG3X,
        EnSerialFmtOnSpeed
    };

    static EnSerialFmt ParseSerialFmt(const std::string& s) {
        if (s == "G3X")     return EnSerialFmtG3X;
        if (s == "ONSPEED") return EnSerialFmtOnSpeed;
        return EnSerialFmtOther;
    }

    // ------------------------------------------------------------------------
    // Data members (all persisted via XML)
    // ------------------------------------------------------------------------

    int             iAoaSmoothing;
    int             iPressureSmoothing;
    int             iMuteAudioUnderIAS;

    // Threshold (knots) below which the firmware blanks the IAS and
    // AOA readout on the display.  Hysteretic: the bit rises through
    // this value and falls through (threshold - 5).  Sentinel 0 means
    // "never blank" (always show IAS/AOA regardless of raw value),
    // matching the iMuteAudioUnderIAS == 0 always-on convention.
    // Default 20 kt — chosen for a typical pitot's noise floor.
    int             iIasDisplayThresholdKt;
    SuDataSource    suDataSrc;
    std::string     sReplayLogFileName;

    std::vector<SuFlaps> aFlaps;

    // Volume
    bool    bVolumeControl;
    int     iVolumeHighAnalog;
    int     iVolumeLowAnalog;
    int     iDefaultVolume;     ///< Percent from 0 to 100
    bool    bAudio3D;           ///< 3D audio enabled
    bool    bOverGWarning;

    // CAS curve
    onspeed::SuCalibrationCurve CasCurve;
    bool                        bCasCurveEnabled;

    // Box orientation
    std::string sPortsOrientation;
    std::string sBoxtopOrientation;
    std::string sEfisType;

    // Calibration data source
    std::string sCalSource;
    bool        bCalSourceEfis;  ///< Cached: sCalSource == "EFIS"

    // Biases
    int     iPFwdBias;      ///< Counts
    int     iP45Bias;       ///< Counts
    float   fPStaticBias;   ///< millibars
    float   fGxBias;
    float   fGyBias;
    float   fGzBias;
    float   fPitchBias;
    float   fRollBias;

    // AHRS algorithm selection: 0=Madgwick (default), 1=EKFQ
    int     iAhrsAlgorithm;

    // Serial inputs
    bool    bReadBoom;
    bool    bReadEfisData;

    // Hardware feature toggles
    bool    bOatSensor;
    bool    bBoomChecksum;

    // Serial output
    std::string sSerialOutFormat;
    EnSerialFmt enSerialOutFormat;  ///< Cached: avoids string compare in 10Hz display loop

    // Load limit
    float   fLoadLimitPositive;
    float   fLoadLimitNegative;

    // Asymmetric G-limit tuning
    float   fAsymmetricGyroLimit;   ///< deg/sec roll/yaw threshold for reduced G-limits
    float   fAsymmetricReduction;   ///< G-limit reduction factor during asymmetric flight

    // Boom data conversion
    bool    bBoomConvertData;       ///< true = apply polynomial conversion, false = raw counts

    // Logging rate
    int     iLogRate;               ///< 50 = pressure rate (default), 208 = IMU rate

    // Vno chime
    int         iVno;                ///< aircraft Vno in kts
    unsigned    uVnoChimeInterval;   ///< chime interval in seconds
    bool        bVnoChimeEnabled;

    // SD card logging
    bool    bSdLogging;

    // Aircraft parameters (used by calibration wizard)
    int     iAcGrossWeight;
    float   fAcBestGlideIAS;    ///< Best glide airspeed at max gross weight (KIAS)
    float   fAcVfe;             ///< Max flap extension speed (KIAS)
    float   fAcGlimit;          ///< Airframe positive load factor limit (G)
    float   fAcNegGlimit;       ///< Airframe negative load factor limit, stored
                                ///< negative (e.g. -1.76f).  Active when the
                                ///< Aircraft category radio is "Custom"; named
                                ///< categories carry hardcoded pos/neg pairs in
                                ///< the web UI.
    float   fCustomAcGlimit;    ///< Pilot's typed Custom positive G, persisted
                                ///< independently of active fAcGlimit so
                                ///< Custom values survive a round trip through
                                ///< a named category.
    float   fCustomAcNegGlimit; ///< Pilot's typed Custom negative G magnitude,
                                ///< stored negative (e.g. -2.5f).

    // Other config data
    char    szDefaultConfigFilename[14] = "onspeed2.cfg";
    bool    bConfigLoaded = false;

    // ------------------------------------------------------------------------
    // Methods
    // ------------------------------------------------------------------------

    /// Reset every field to its compile-time default.  Called by the default
    /// constructor and again when the sketch detects a missing/corrupt config.
    bool LoadDefaults();
};

// ============================================================================
// Free helpers
// ============================================================================

/// Invariant: aFlaps.size() >= 1.  Every reader (Audio, SensorIO,
/// DisplaySerial, DataServer, LogReplay) dereferences
/// aFlaps[g_Flaps.iIndex] without bounds-checking, so the vector must
/// always have at least one entry.  Call this after any path that can
/// leave it empty — notably the web UI save handler, which clears and
/// rebuilds the vector based on user-marked deletions.
///
/// If the vector is empty, pushes one zeroed SuFlaps with the same
/// shape LoadDefaults produces (all setpoints 0, polynomial curve
/// type with zero coefficients).  The zero setpoints are the explicit
/// "uncalibrated" signal the audio tone gate in ToneCalc looks for.
///
/// Returns true when a default was pushed, false when aFlaps already
/// had entries.
bool EnsureAtLeastOneFlap(std::vector<OnSpeedConfig::SuFlaps>& aFlaps);

}  // namespace onspeed::config

#endif  // ONSPEED_CORE_CONFIG_ONSPEED_CONFIG_H
