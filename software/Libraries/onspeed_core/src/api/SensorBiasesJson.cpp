// SensorBiasesJson.cpp — pure JSON serializer for GET /api/sensors/biases.

#include "SensorBiasesJson.h"

#include <cmath>
#include <cstdio>

namespace onspeed::api {

namespace {

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

const char* SourceTag(EfisBaroSource s) {
    switch (s) {
        case EfisBaroSource::None:  return "none";
        case EfisBaroSource::Vn300: return "vn300";
        case EfisBaroSource::Baro:  return "baro";
    }
    return "none";
}

}  // namespace

std::string SerializeSensorBiases(const SensorBiasesInputs& in) {
    std::string out;
    out.reserve(512);

    out += "{\"biases\":{";
    out += "\"pFwdCounts\":";
    AppendInt(out, in.pFwdBiasCounts);
    out += ",\"p45Counts\":";
    AppendInt(out, in.p45BiasCounts);
    out += ",\"pStaticMb\":";
    AppendFloat(out, in.pStaticBiasMb);
    out += ",\"gxDegPerSec\":";
    AppendFloat(out, in.gxBias);
    out += ",\"gyDegPerSec\":";
    AppendFloat(out, in.gyBias);
    out += ",\"gzDegPerSec\":";
    AppendFloat(out, in.gzBias);
    out += ",\"pitchDeg\":";
    AppendFloat(out, in.pitchBiasDeg);
    out += ",\"rollDeg\":";
    AppendFloat(out, in.rollBiasDeg);
    out += ",\"oatRecoveryFactor\":";
    AppendFloat(out, in.oatRecoveryFactor);

    out += "},\"live\":{";
    out += "\"imuPitchDeg\":";
    AppendFloat(out, in.imuPitchDeg);
    out += ",\"imuRollDeg\":";
    AppendFloat(out, in.imuRollDeg);
    out += ",\"truePitchDeg\":";
    AppendFloat(out, in.truePitchDeg);
    out += ",\"trueRollDeg\":";
    AppendFloat(out, in.trueRollDeg);

    out += "},\"efis\":{";
    out += "\"source\":\"";
    out += SourceTag(in.efisSource);
    out += "\",\"pitchDeg\":";
    AppendFloat(out, in.efisPitchDeg);
    out += ",\"rollDeg\":";
    AppendFloat(out, in.efisRollDeg);
    out += ",\"paltFt\":";
    AppendFloat(out, in.efisPaltFt);

    out += "}}";
    return out;
}

}  // namespace onspeed::api
