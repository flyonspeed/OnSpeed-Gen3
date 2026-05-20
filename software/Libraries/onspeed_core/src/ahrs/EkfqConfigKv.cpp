#include <ahrs/EkfqConfigKv.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace onspeed::ahrs {

namespace {

// Trim leading/trailing ASCII whitespace from a string_view.
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

bool parse_float(std::string_view s, float& out) {
    // string_view → null-terminated tmp; std::stof is the path of least
    // resistance and matches what the rest of host_main does.
    try {
        std::string tmp(s);
        size_t consumed = 0;
        out = std::stof(tmp, &consumed);
        // Reject trailing junk.
        return consumed == tmp.size();
    } catch (...) {
        return false;
    }
}

}   // namespace

bool ParseEkfqConfigKv(std::string_view text,
                       onspeed::EKFQ::Config& outEkfqCfg,
                       EkfqPipeline::PipelineConfig& outPipeCfg,
                       std::function<void(const char*)> warnSink)
{
    auto warn = [&](const std::string& msg) {
        if (warnSink) warnSink(msg.c_str());
    };

    // Initialise outputs to defaults.
    outEkfqCfg  = onspeed::EKFQ::Config::defaults();
    outPipeCfg  = EkfqPipeline::PipelineConfig::defaults();

    // Map each known key to a writer lambda. The lambda owns the
    // target-pointer indirection; the dispatch is just a hash lookup.
    using Writer = bool(*)(float, onspeed::EKFQ::Config&,
                           EkfqPipeline::PipelineConfig&);
    const std::unordered_map<std::string, Writer> writers = {
        {"q_quat",       [](float v, auto& e, auto&){ e.q_quat = v; return true; }},
        {"q_bias",       [](float v, auto& e, auto&){ e.q_bias = v; return true; }},
        {"q_z",          [](float v, auto& e, auto&){ e.q_z    = v; return true; }},
        {"q_vz",         [](float v, auto& e, auto&){ e.q_vz   = v; return true; }},
        {"q_b_az",       [](float v, auto& e, auto&){ e.q_b_az = v; return true; }},
        {"q_beta",       [](float v, auto& e, auto&){ e.q_beta = v; return true; }},
        {"r_ax",         [](float v, auto& e, auto&){ e.r_ax   = v; return true; }},
        {"r_ay",         [](float v, auto& e, auto&){ e.r_ay   = v; return true; }},
        {"r_az",         [](float v, auto& e, auto&){ e.r_az   = v; return true; }},
        {"r_baro",       [](float v, auto& e, auto&){ e.r_baro = v; return true; }},
        {"r_beta_prior", [](float v, auto& e, auto&){ e.r_beta_prior = v; return true; }},
        {"r_bias_prior", [](float v, auto& e, auto&){ e.r_bias_prior = v; return true; }},
        {"k_beta_r",     [](float v, auto& e, auto&){ e.k_beta_R = v; return true; }},
        {"p_quat",       [](float v, auto& e, auto&){ e.p_quat = v; return true; }},
        {"p_bias",       [](float v, auto& e, auto&){ e.p_bias = v; return true; }},
        {"p_z",          [](float v, auto& e, auto&){ e.p_z    = v; return true; }},
        {"p_vz",         [](float v, auto& e, auto&){ e.p_vz   = v; return true; }},
        {"p_b_az",       [](float v, auto& e, auto&){ e.p_b_az = v; return true; }},
        {"p_beta",       [](float v, auto& e, auto&){ e.p_beta = v; return true; }},
        {"tas_min_mps",  [](float v, auto& e, auto&){ e.tas_min_mps = v; return true; }},
        {"accel_ema_alpha",     [](float v, auto&, auto& p){ p.accelEmaAlpha   = v; return true; }},
        {"comp_fade_tau_sec",   [](float v, auto&, auto& p){ p.compFadeTauSec  = v; return true; }},
        {"ias_gate_rising_kt",  [](float v, auto&, auto& p){ p.iasGateRisingKt = v; return true; }},
        {"tasdot_ema_alpha",    [](float v, auto&, auto& p){ p.tasdotEmaAlpha  = v; return true; }},
    };

    // Track which keys were seen so we can warn on the rest.
    std::unordered_map<std::string, bool> seen;
    for (const auto& [k, _] : writers) seen[k] = false;

    // Walk lines.
    size_t lineNum = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t eol = text.find('\n', pos);
        const std::string_view raw = text.substr(
            pos, (eol == std::string_view::npos) ? text.size() - pos : eol - pos);
        pos = (eol == std::string_view::npos) ? text.size() : eol + 1;
        ++lineNum;

        const std::string_view line = trim(raw);
        if (line.empty() || line.front() == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: line %zu malformed (no '='): '%.*s'",
                lineNum, (int)line.size(), line.data());
            warn(buf);
            return false;
        }
        const std::string key(trim(line.substr(0, eq)));
        const std::string_view valSv = trim(line.substr(eq + 1));

        auto it = writers.find(key);
        if (it == writers.end()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: line %zu unknown key '%s'",
                lineNum, key.c_str());
            warn(buf);
            return false;
        }

        float v = 0.0f;
        if (!parse_float(valSv, v)) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: line %zu cannot parse '%.*s' as float for key '%s'",
                lineNum, (int)valSv.size(), valSv.data(), key.c_str());
            warn(buf);
            return false;
        }
        (void)it->second(v, outEkfqCfg, outPipeCfg);
        seen[key] = true;
    }

    // Warn on missing keys (defaults already applied).
    for (const auto& [k, wasSeen] : seen) {
        if (!wasSeen) {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "EkfqConfigKv: key '%s' missing — using default", k.c_str());
            warn(buf);
        }
    }
    return true;
}

}   // namespace onspeed::ahrs
