// LogRowToAhrsInputs.h
//
// Translates a logged LogRow into the AhrsInputs shape that Ahrs::Step
// consumes live. Holds the small amount of inter-row state needed
// (previous timestamp, previous PfwdSmoothed for fresh-pressure
// synthesis). One instance per replay session.
//
// This is the bridge between the "logged" pipeline-stage representation
// (LogRow, what proto::log_csv::ParseRowByIndex produces) and the
// "live" pipeline-stage representation (AhrsInputs, what Ahrs::Step
// consumes). Used by host_main ahrs_tone --input-format=sdlog and by
// any future replay caller that wants to re-execute the AHRS stage
// from raw inputs.

#ifndef ONSPEED_CORE_REPLAY_LOG_ROW_TO_AHRS_INPUTS_H
#define ONSPEED_CORE_REPLAY_LOG_ROW_TO_AHRS_INPUTS_H

#include <types/AhrsInputs.h>
#include <types/LogRow.h>

namespace onspeed::replay {

class LogRowToAhrsInputs {
public:
    LogRowToAhrsInputs() = default;

    /// Result of one row translation.
    struct Result {
        AhrsInputs inputs;
        /// Seconds since the previous row. 1/208 on the first row
        /// (no prior timestamp to diff against).
        float dtSec;
        /// True on the row this caller should pass to Ahrs::Init,
        /// not Ahrs::Step. Always true for the first row; false
        /// thereafter.
        bool isSeedFrame;
    };

    /// Translate one logged row. Updates internal state for dt and
    /// fresh-pressure synthesis.
    Result translate(const onspeed::LogRow& row);

    /// Reset inter-row state (call between separate replay sessions).
    void reset();

private:
    bool initialized_ = false;
    uint64_t prevTimeStampUs_ = 0;
    float    prevPfwdSmoothed_ = 0.0f;
    uint32_t synthIasUpdateUs_ = 0;
};

}   // namespace onspeed::replay

#endif   // ONSPEED_CORE_REPLAY_LOG_ROW_TO_AHRS_INPUTS_H
