// proto/LogCsvHeaderIndex.cpp — name-keyed CSV log replay support.
//
// See LogCsvHeaderIndex.h for the contract. This file holds the column-name
// to ordinal binding table, the always-present column validation loop, and a
// stub ParseRowByIndex that subsequent commits fill in.

#include <proto/LogCsvHeaderIndex.h>

#include <cstring>
#include <string_view>

namespace onspeed::proto::log_csv {

namespace {

// Strip trailing CR/LF from a token.
std::string_view RstripCrLf(std::string_view s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

// Try to bind one token to a known column name. Returns true if matched
// (and the corresponding HeaderIndex field was set). Optional-group columns
// (boom, EFIS, VN-300) are intentionally not handled here — they land in a
// follow-up commit alongside the group-validation logic.
bool BindKnownColumn(std::string_view name, int ordinal, HeaderIndex& out)
{
    // Always-present core columns.
    if (name == "timeStamp")        { out.idxTimeStampMs = ordinal; return true; }
    if (name == "Pfwd")             { out.idxPfwd = ordinal; return true; }
    if (name == "PfwdSmoothed")     { out.idxPfwdSmoothed = ordinal; return true; }
    if (name == "P45")              { out.idxP45 = ordinal; return true; }
    if (name == "P45Smoothed")      { out.idxP45Smoothed = ordinal; return true; }
    if (name == "PStatic")          { out.idxPStatic = ordinal; return true; }
    if (name == "Palt")             { out.idxPaltFt = ordinal; return true; }
    if (name == "IAS")              { out.idxIasKt = ordinal; return true; }
    if (name == "AngleofAttack")    { out.idxAoaDeg = ordinal; return true; }
    if (name == "flapsPos")         { out.idxFlapsPos = ordinal; return true; }
    if (name == "DataMark")         { out.idxDataMark = ordinal; return true; }
    if (name == "OAT")              { out.idxOatCelsius = ordinal; return true; }
    if (name == "TAS")              { out.idxTasKt = ordinal; return true; }
    if (name == "imuTemp")          { out.idxImuTemp = ordinal; return true; }
    if (name == "VerticalG")        { out.idxVerticalG = ordinal; return true; }
    if (name == "LateralG")         { out.idxLateralG = ordinal; return true; }
    if (name == "ForwardG")         { out.idxForwardG = ordinal; return true; }
    if (name == "RollRate")         { out.idxRollRate = ordinal; return true; }
    if (name == "PitchRate")        { out.idxPitchRate = ordinal; return true; }
    if (name == "YawRate")          { out.idxYawRate = ordinal; return true; }
    if (name == "Pitch")            { out.idxPitchDeg = ordinal; return true; }
    if (name == "Roll")             { out.idxRollDeg = ordinal; return true; }

    // Always-present derived.
    if (name == "EarthVerticalG")   { out.idxEarthVerticalG = ordinal; return true; }
    if (name == "FlightPath")       { out.idxFlightPathDeg = ordinal; return true; }
    if (name == "VSI")              { out.idxVsiFpm = ordinal; return true; }
    if (name == "Altitude")         { out.idxAltitude = ordinal; return true; }
    if (name == "DerivedAOA")       { out.idxDerivedAoa = ordinal; return true; }
    if (name == "CoeffP")           { out.idxCoeffP = ordinal; return true; }

    // Optional groups (boom, standard EFIS, VN-300) are handled in a
    // follow-up commit. Tokens that match those names fall through here
    // and are counted as unknown for now.
    return false;
}

void EmitWarn(WarnSink warnSink, void* userdata, const char* missing)
{
    if (warnSink) warnSink(missing, userdata);
}

// Validate a known column is present.
//   Strict     — return false on absence and set *missingOut.
//   Permissive — emit a warning, leave the field at -1, return true.
bool RequireColumn(int idx, const char* name,
                   HeaderStrictness strictness,
                   const char** missingOut,
                   WarnSink warnSink, void* warnUserdata)
{
    if (idx >= 0) return true;
    if (strictness == HeaderStrictness::Strict) {
        if (missingOut) *missingOut = name;
        return false;
    }
    EmitWarn(warnSink, warnUserdata, name);
    return true;
}

}  // namespace

bool BuildHeaderIndex(std::string_view headerLine,
                      HeaderIndex& out,
                      HeaderStrictness strictness,
                      const char** missingOut,
                      WarnSink warnSink,
                      void* warnUserdata)
{
    out = HeaderIndex{};   // reset to defaults
    if (missingOut) *missingOut = nullptr;

    int ordinal = 0;
    size_t pos = 0;
    while (pos <= headerLine.size())
    {
        size_t comma = headerLine.find(',', pos);
        std::string_view tok =
            (comma == std::string_view::npos)
                ? headerLine.substr(pos)
                : headerLine.substr(pos, comma - pos);
        tok = RstripCrLf(tok);

        BindKnownColumn(tok, ordinal, out);
        ordinal++;

        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    out.totalColumns = ordinal;

    // Validate always-present core columns. Order of checks matches
    // canonical column order so the failure message points at the
    // earliest missing column.
    #define REQUIRE(field, name) \
        if (!RequireColumn(out.field, name, strictness, missingOut, \
                           warnSink, warnUserdata)) return false

    REQUIRE(idxTimeStampMs,   "timeStamp");
    REQUIRE(idxPfwd,          "Pfwd");
    REQUIRE(idxPfwdSmoothed,  "PfwdSmoothed");
    REQUIRE(idxP45,           "P45");
    REQUIRE(idxP45Smoothed,   "P45Smoothed");
    REQUIRE(idxPStatic,       "PStatic");
    REQUIRE(idxPaltFt,        "Palt");
    REQUIRE(idxIasKt,         "IAS");
    REQUIRE(idxAoaDeg,        "AngleofAttack");
    REQUIRE(idxFlapsPos,      "flapsPos");
    REQUIRE(idxDataMark,      "DataMark");
    REQUIRE(idxOatCelsius,    "OAT");
    REQUIRE(idxTasKt,         "TAS");
    REQUIRE(idxImuTemp,       "imuTemp");
    REQUIRE(idxVerticalG,     "VerticalG");
    REQUIRE(idxLateralG,      "LateralG");
    REQUIRE(idxForwardG,      "ForwardG");
    REQUIRE(idxRollRate,      "RollRate");
    REQUIRE(idxPitchRate,     "PitchRate");
    REQUIRE(idxYawRate,       "YawRate");
    REQUIRE(idxPitchDeg,      "Pitch");
    REQUIRE(idxRollDeg,       "Roll");

    REQUIRE(idxEarthVerticalG, "EarthVerticalG");
    REQUIRE(idxFlightPathDeg,  "FlightPath");
    REQUIRE(idxVsiFpm,         "VSI");
    REQUIRE(idxAltitude,       "Altitude");
    REQUIRE(idxDerivedAoa,     "DerivedAOA");
    REQUIRE(idxCoeffP,         "CoeffP");

    #undef REQUIRE

    // Optional groups (boom, standard EFIS, VN-300) stay false in this
    // skeleton. Group validation lands in a follow-up commit.
    return true;
}

// ParseRowByIndex stub: filled in a follow-up commit. Returns false so any
// caller that wires it up before the implementation lands gets a clear
// failure rather than silently zeroed rows.
bool ParseRowByIndex(std::string_view, const HeaderIndex&, onspeed::LogRow&)
{
    return false;
}

}  // namespace onspeed::proto::log_csv
