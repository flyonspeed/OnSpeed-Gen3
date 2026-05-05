// CalwizStateJson.cpp — pure JSON serializer for GET /api/calwiz/state.

#include "CalwizStateJson.h"

#include <cstdio>
#include <cmath>

namespace onspeed::api {

namespace {

// Append %.6g formatted float; finite values only.  Non-finite (NaN,
// Inf) are emitted as 0 to keep the JSON parser-safe; the calwiz UI
// treats 0 setpoints as "uncalibrated", matching ToneCalc's gating.
void AppendFloat(std::string& out, float v) {
    char buf[32];
    if (!std::isfinite(v)) v = 0.0f;
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
    out += buf;
}

void AppendInt(std::string& out, int v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", v);
    out += buf;
}

}  // namespace

std::string SerializeCalwizState(const CalwizStateInputs& in) {
    std::string out;
    out.reserve(512);

    out += "{\"aircraft\":{";
    out += "\"grossWeightLb\":";
    AppendInt(out, in.acGrossWeightLb);
    out += ",\"bestGlideKt\":";
    AppendFloat(out, in.acBestGlideKt);
    out += ",\"vfeKt\":";
    AppendFloat(out, in.acVfeKt);
    out += ",\"gLimit\":";
    AppendFloat(out, in.acGLimit);
    out += "},\"currentFlapIndex\":";

    int idx = in.currentFlapIndex;
    if (idx < 0 || (size_t)idx >= in.flaps.size()) idx = 0;
    AppendInt(out, idx);

    out += ",\"flaps\":[";
    for (size_t i = 0; i < in.flaps.size(); ++i) {
        if (i > 0) out += ',';
        const auto& f = in.flaps[i];
        out += "{\"index\":";
        AppendInt(out, static_cast<int>(i));
        out += ",\"degrees\":";
        AppendInt(out, f.iDegrees);
        out += ",\"alpha0Deg\":";
        AppendFloat(out, f.fAlpha0);
        out += ",\"alphaStallDeg\":";
        AppendFloat(out, f.fAlphaStall);
        out += ",\"ldMaxAoaDeg\":";
        AppendFloat(out, f.fLDMAXAOA);
        out += ",\"onSpeedFastAoaDeg\":";
        AppendFloat(out, f.fONSPEEDFASTAOA);
        out += ",\"onSpeedSlowAoaDeg\":";
        AppendFloat(out, f.fONSPEEDSLOWAOA);
        out += ",\"stallWarnAoaDeg\":";
        AppendFloat(out, f.fSTALLWARNAOA);
        out += ",\"stallAoaDeg\":";
        AppendFloat(out, f.fSTALLAOA);
        out += ",\"maneuveringAoaDeg\":";
        AppendFloat(out, f.fMANAOA);
        out += '}';
    }
    out += "]}";
    return out;
}

}  // namespace onspeed::api
