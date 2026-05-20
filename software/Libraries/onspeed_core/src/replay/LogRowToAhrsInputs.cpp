#include <replay/LogRowToAhrsInputs.h>

#include <cmath>

namespace onspeed::replay {

namespace {
constexpr float kDefaultDtSec = 1.0f / 208.0f;
// ~50 Hz pressure cadence (1e6 us / 50 Hz = 20000 us between fresh samples).
constexpr uint32_t kPressurePeriodUs = 20000;
}   // namespace

void LogRowToAhrsInputs::reset() {
    initialized_       = false;
    prevTimeStampUs_   = 0;
    prevPfwdSmoothed_  = 0.0f;
    synthIasUpdateUs_  = 0;
}

LogRowToAhrsInputs::Result
LogRowToAhrsInputs::translate(const onspeed::LogRow& row) {
    Result out{};

    // Map IMU + sensors directly. PitchRate stays as-is — LogRow holds
    // the un-negated firmware-internal value (LogCsv wire flip is undone
    // by ParseRowByIndex on read).
    out.inputs.imu.accelXG       = row.imuForwardG;
    out.inputs.imu.accelYG       = row.imuLateralG;
    out.inputs.imu.accelZG       = row.imuVerticalG;
    out.inputs.imu.gyroRollDps   = row.imuRollRateDps;
    out.inputs.imu.gyroPitchDps  = row.imuPitchRateDps;
    out.inputs.imu.gyroYawDps    = row.imuYawRateDps;
    out.inputs.imu.tempCelsius   = row.imuTempCelsius;
    out.inputs.imu.timestampUs   = (uint32_t)(row.timeStampUs & 0xFFFFFFFFu);

    out.inputs.sensors.iasKt      = row.iasKt;
    out.inputs.sensors.paltFt     = row.paltFt;
    out.inputs.sensors.oatCelsius = row.oatCelsius;
    out.inputs.sensors.iasAlive   = std::isfinite(row.iasKt) && row.iasKt > 0.0f;

    out.inputs.useEfisOat     = false;
    out.inputs.useInternalOat = true;
    out.inputs.efisOatCelsius = 0.0f;

    // Synthesize iasUpdateTimestampUs from PfwdSmoothed diff. This is
    // the same heuristic Lenny's data.py uses: when the smoothed
    // value changes, a new pressure-stage sample arrived ~now.
    if (initialized_ && row.pfwdSmoothed != prevPfwdSmoothed_) {
        synthIasUpdateUs_ += kPressurePeriodUs;
    } else if (!initialized_) {
        // Seed the synth timestamp on the first frame.
        synthIasUpdateUs_ = kPressurePeriodUs;
    }
    prevPfwdSmoothed_ = row.pfwdSmoothed;
    out.inputs.iasUpdateTimestampUs = synthIasUpdateUs_;

    // dt computation.
    if (!initialized_) {
        out.dtSec       = kDefaultDtSec;
        out.isSeedFrame = true;
        initialized_    = true;
    } else {
        if (row.timeStampUs > prevTimeStampUs_) {
            out.dtSec = (float)(row.timeStampUs - prevTimeStampUs_) * 1.0e-6f;
        } else {
            out.dtSec = kDefaultDtSec;
        }
        out.isSeedFrame = false;
    }
    prevTimeStampUs_ = row.timeStampUs;

    return out;
}

}   // namespace onspeed::replay
