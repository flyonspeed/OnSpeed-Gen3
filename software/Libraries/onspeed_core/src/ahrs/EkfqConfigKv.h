// EkfqConfigKv.h — kv-text parser for EKFQ::Config + EkfqPipeline::PipelineConfig.
//
// Format:
//   key=value          one per line
//   # comment          lines starting with '#' are ignored
//   <blank>            blank lines are ignored
//
// Keys (case-sensitive, all lowercase with underscores):
//   EKFQ::Config (20):
//     q_quat, q_bias, q_z, q_vz, q_b_az, q_beta,
//     r_ax, r_ay, r_az, r_baro, r_beta_prior, r_bias_prior, k_beta_r,
//     p_quat, p_bias, p_z, p_vz, p_b_az, p_beta,
//     tas_min_mps
//   PipelineConfig (4):
//     accel_ema_alpha, comp_fade_tau_sec, ias_gate_rising_kt, tasdot_ema_alpha
//
// Unknown keys cause the parser to return false (typo guard for tuner
// scripts). Missing keys are warned (via warnSink) and left at defaults
// so partial-override files work.

#ifndef ONSPEED_CORE_AHRS_EKFQ_CONFIG_KV_H
#define ONSPEED_CORE_AHRS_EKFQ_CONFIG_KV_H

#include <string_view>
#include <functional>

#include <ahrs/EKFQ.h>
#include <ahrs/EkfqPipeline.h>

namespace onspeed::ahrs {

/// Parse a kv-format text blob into EKFQ + Pipeline configs.
///
/// On success: returns true, outEkfqCfg + outPipeCfg are populated.
///   Unset keys are filled from EKFQ::Config::defaults() and
///   PipelineConfig::defaults(); warnSink is called per missing key.
/// On failure (unknown key, malformed line, parse error):
///   returns false, calls warnSink with a descriptive message.
///
/// warnSink may be null; if so, warnings are silently dropped.
bool ParseEkfqConfigKv(std::string_view text,
                       onspeed::EKFQ::Config& outEkfqCfg,
                       EkfqPipeline::PipelineConfig& outPipeCfg,
                       std::function<void(const char*)> warnSink = {});

}   // namespace onspeed::ahrs

#endif   // ONSPEED_CORE_AHRS_EKFQ_CONFIG_KV_H
