// proto/LogCsvHeaderIndex.cpp — name-keyed CSV log replay support.
//
// See LogCsvHeaderIndex.h for the contract. This file holds the column-name
// to ordinal binding table, the always-present column validation loop, and a
// stub ParseRowByIndex that subsequent commits fill in.

#include <proto/LogCsvHeaderIndex.h>

#include <cstdlib>
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
// (and the corresponding HeaderIndex field was set). Group membership
// (boom, standard EFIS, VN-300) is decided later in BuildHeaderIndex; this
// helper only records ordinals for any column it recognizes.
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

    // Tail-optional (format version 2). Older logs lack this column.
    if (name == "flapsRawADC")      { out.idxFlapsRawAdc = ordinal; return true; }

    // Boom (optional).
    if (name == "boomStatic")       { out.idxBoomStatic = ordinal; return true; }
    if (name == "boomDynamic")      { out.idxBoomDynamic = ordinal; return true; }
    if (name == "boomAlpha")        { out.idxBoomAlpha = ordinal; return true; }
    if (name == "boomBeta")         { out.idxBoomBeta = ordinal; return true; }
    if (name == "boomIAS")          { out.idxBoomIas = ordinal; return true; }
    if (name == "boomAge")          { out.idxBoomAge = ordinal; return true; }

    // EFIS — standard set (optional).
    if (name == "efisIAS")          { out.idxEfisIas = ordinal; return true; }
    if (name == "efisPitch")        { out.idxEfisPitch = ordinal; return true; }
    if (name == "efisRoll")         { out.idxEfisRoll = ordinal; return true; }
    if (name == "efisLateralG")     { out.idxEfisLateralG = ordinal; return true; }
    if (name == "efisVerticalG")    { out.idxEfisVerticalG = ordinal; return true; }
    if (name == "efisPercentLift")  { out.idxEfisPercentLift = ordinal; return true; }
    if (name == "efisPalt")         { out.idxEfisPalt = ordinal; return true; }
    if (name == "efisVSI")          { out.idxEfisVsi = ordinal; return true; }
    if (name == "efisTAS")          { out.idxEfisTas = ordinal; return true; }
    if (name == "efisOAT")          { out.idxEfisOat = ordinal; return true; }
    if (name == "efisFuelRemaining"){ out.idxEfisFuelRemaining = ordinal; return true; }
    if (name == "efisFuelFlow")     { out.idxEfisFuelFlow = ordinal; return true; }
    if (name == "efisMAP")          { out.idxEfisMap = ordinal; return true; }
    if (name == "efisRPM")          { out.idxEfisRpm = ordinal; return true; }
    if (name == "efisPercentPower") { out.idxEfisPercentPower = ordinal; return true; }
    if (name == "efisMagHeading")   { out.idxEfisMagHeading = ordinal; return true; }
    if (name == "efisAge")          { out.idxEfisAge = ordinal; return true; }
    if (name == "efisTime")         { out.idxEfisTime = ordinal; return true; }

    // EFIS — VN-300 set (optional).
    if (name == "vnAngularRateRoll")  { out.idxVnAngularRateRoll = ordinal; return true; }
    if (name == "vnAngularRatePitch") { out.idxVnAngularRatePitch = ordinal; return true; }
    if (name == "vnAngularRateYaw")   { out.idxVnAngularRateYaw = ordinal; return true; }
    if (name == "vnVelNedNorth")      { out.idxVnVelNedNorth = ordinal; return true; }
    if (name == "vnVelNedEast")       { out.idxVnVelNedEast = ordinal; return true; }
    if (name == "vnVelNedDown")       { out.idxVnVelNedDown = ordinal; return true; }
    if (name == "vnAccelFwd")         { out.idxVnAccelFwd = ordinal; return true; }
    if (name == "vnAccelLat")         { out.idxVnAccelLat = ordinal; return true; }
    if (name == "vnAccelVert")        { out.idxVnAccelVert = ordinal; return true; }
    if (name == "vnYaw")              { out.idxVnYaw = ordinal; return true; }
    if (name == "vnPitch")            { out.idxVnPitch = ordinal; return true; }
    if (name == "vnRoll")             { out.idxVnRoll = ordinal; return true; }
    if (name == "vnLinAccFwd")        { out.idxVnLinAccFwd = ordinal; return true; }
    if (name == "vnLinAccLat")        { out.idxVnLinAccLat = ordinal; return true; }
    if (name == "vnLinAccVert")       { out.idxVnLinAccVert = ordinal; return true; }
    if (name == "vnYawSigma")         { out.idxVnYawSigma = ordinal; return true; }
    if (name == "vnRollSigma")        { out.idxVnRollSigma = ordinal; return true; }
    if (name == "vnPitchSigma")       { out.idxVnPitchSigma = ordinal; return true; }
    if (name == "vnGnssVelNedNorth")  { out.idxVnGnssVelNedNorth = ordinal; return true; }
    if (name == "vnGnssVelNedEast")   { out.idxVnGnssVelNedEast = ordinal; return true; }
    if (name == "vnGnssVelNedDown")   { out.idxVnGnssVelNedDown = ordinal; return true; }
    if (name == "vnGnssLat")          { out.idxVnGnssLat = ordinal; return true; }
    if (name == "vnGnssLon")          { out.idxVnGnssLon = ordinal; return true; }
    if (name == "vnGPSFix")           { out.idxVnGpsFix = ordinal; return true; }
    if (name == "vnDataAge")          { out.idxVnDataAge = ordinal; return true; }
    if (name == "vnTimeUTC")          { out.idxVnTimeUtc = ordinal; return true; }

    return false;
}

void EmitWarn(WarnSink warnSink, const char* missing)
{
    if (warnSink) warnSink(missing);
}

// Validate a known column is present. Emits a warning when absent and
// leaves the field at -1.
void RequireColumn(int idx, const char* name, WarnSink warnSink)
{
    if (idx >= 0) return;
    EmitWarn(warnSink, name);
}

}  // namespace

bool BuildHeaderIndex(std::string_view headerLine,
                      HeaderIndex& out,
                      WarnSink warnSink)
{
    out = HeaderIndex{};   // reset to defaults

    int ordinal = 0;
    int matchedKnown = 0;
    size_t pos = 0;
    while (pos <= headerLine.size())
    {
        size_t comma = headerLine.find(',', pos);
        std::string_view tok =
            (comma == std::string_view::npos)
                ? headerLine.substr(pos)
                : headerLine.substr(pos, comma - pos);
        tok = RstripCrLf(tok);

        if (BindKnownColumn(tok, ordinal, out)) matchedKnown++;
        ordinal++;

        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    out.totalColumns = ordinal;

    // Refuse a header that overshoots the row-side tokenizer cap
    // (TokenizeRow truncates at kHeaderIndexMaxColumns). Without this
    // ParseRowByIndex's `tokenCount < idx.totalColumns` check returns
    // false for every row and replay drops the entire log silently.
    if (ordinal > kHeaderIndexMaxColumns) {
        EmitWarn(warnSink, "(header exceeds kHeaderIndexMaxColumns)");
        return false;
    }

    // Refuse a header that contains zero recognized OnSpeed columns.
    // Without this we'd warn for every required column and return true,
    // leaving every idxXxx == -1; ParseRowByIndex then produces all-zero
    // LogRows and replay runs the audio engine with bogus inputs to EOF.
    if (matchedKnown == 0) {
        EmitWarn(warnSink, "(no recognized OnSpeed columns in header)");
        return false;
    }

    // Validate always-present core columns. Order of checks matches
    // canonical column order so warnings fire in canonical order.
    #define REQUIRE(field, name) \
        RequireColumn(out.field, name, warnSink)

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

    // Optional-group resolution. A group is "present" only when every
    // column in it appears in the header. Any column from a group present
    // without the rest is treated as schema drift: emit one warning naming
    // the first missing column, leave the Enabled flag false. Present
    // columns within a partial group keep their ordinals (debug
    // visibility), but ParseRowByIndex won't unpack the group because the
    // Enabled flag stays false.

    auto allOf = [](const int* idxs, int count) -> bool {
        for (int i = 0; i < count; ++i) if (idxs[i] < 0) return false;
        return true;
    };
    auto firstMissing = [](const int* idxs, const char* const* names, int count) -> const char* {
        for (int i = 0; i < count; ++i) if (idxs[i] < 0) return names[i];
        return nullptr;
    };

    // Boom: any boom column present means all six must be.
    {
        const int idxs[]  = { out.idxBoomStatic, out.idxBoomDynamic, out.idxBoomAlpha,
                              out.idxBoomBeta,   out.idxBoomIas,     out.idxBoomAge };
        const char* names[] = { "boomStatic", "boomDynamic", "boomAlpha",
                                "boomBeta",   "boomIAS",     "boomAge" };
        bool any = false;
        for (int i = 0; i < 6; ++i) if (idxs[i] >= 0) { any = true; break; }
        if (any) {
            if (!allOf(idxs, 6)) {
                EmitWarn(warnSink, firstMissing(idxs, names, 6));
            } else {
                out.boomEnabled = true;
            }
        }
    }

    // VN-300: any VN-300 column present means all 26 must be. Resolved
    // before standard EFIS so the !efisIsVn300 guard below works.
    {
        const int idxs[]  = {
            out.idxVnAngularRateRoll, out.idxVnAngularRatePitch, out.idxVnAngularRateYaw,
            out.idxVnVelNedNorth, out.idxVnVelNedEast, out.idxVnVelNedDown,
            out.idxVnAccelFwd, out.idxVnAccelLat, out.idxVnAccelVert,
            out.idxVnYaw, out.idxVnPitch, out.idxVnRoll,
            out.idxVnLinAccFwd, out.idxVnLinAccLat, out.idxVnLinAccVert,
            out.idxVnYawSigma, out.idxVnRollSigma, out.idxVnPitchSigma,
            out.idxVnGnssVelNedNorth, out.idxVnGnssVelNedEast, out.idxVnGnssVelNedDown,
            out.idxVnGnssLat, out.idxVnGnssLon, out.idxVnGpsFix,
            out.idxVnDataAge, out.idxVnTimeUtc };
        const char* names[] = {
            "vnAngularRateRoll", "vnAngularRatePitch", "vnAngularRateYaw",
            "vnVelNedNorth", "vnVelNedEast", "vnVelNedDown",
            "vnAccelFwd", "vnAccelLat", "vnAccelVert",
            "vnYaw", "vnPitch", "vnRoll",
            "vnLinAccFwd", "vnLinAccLat", "vnLinAccVert",
            "vnYawSigma", "vnRollSigma", "vnPitchSigma",
            "vnGnssVelNedNorth", "vnGnssVelNedEast", "vnGnssVelNedDown",
            "vnGnssLat", "vnGnssLon", "vnGPSFix",
            "vnDataAge", "vnTimeUTC" };
        bool any = false;
        for (int i = 0; i < 26; ++i) if (idxs[i] >= 0) { any = true; break; }
        if (any) {
            if (!allOf(idxs, 26)) {
                EmitWarn(warnSink, firstMissing(idxs, names, 26));
            } else {
                out.efisIsVn300 = true;
                out.efisEnabled = true;
            }
        }
    }

    // Standard EFIS (only when not VN-300).
    if (!out.efisIsVn300) {
        const int idxs[]  = {
            out.idxEfisIas, out.idxEfisPitch, out.idxEfisRoll, out.idxEfisLateralG,
            out.idxEfisVerticalG, out.idxEfisPercentLift, out.idxEfisPalt, out.idxEfisVsi,
            out.idxEfisTas, out.idxEfisOat, out.idxEfisFuelRemaining, out.idxEfisFuelFlow,
            out.idxEfisMap, out.idxEfisRpm, out.idxEfisPercentPower, out.idxEfisMagHeading,
            out.idxEfisAge, out.idxEfisTime };
        const char* names[] = {
            "efisIAS", "efisPitch", "efisRoll", "efisLateralG",
            "efisVerticalG", "efisPercentLift", "efisPalt", "efisVSI",
            "efisTAS", "efisOAT", "efisFuelRemaining", "efisFuelFlow",
            "efisMAP", "efisRPM", "efisPercentPower", "efisMagHeading",
            "efisAge", "efisTime" };
        bool any = false;
        for (int i = 0; i < 18; ++i) if (idxs[i] >= 0) { any = true; break; }
        if (any) {
            if (!allOf(idxs, 18)) {
                EmitWarn(warnSink, firstMissing(idxs, names, 18));
            } else {
                out.efisEnabled = true;
            }
        }
    }

    return true;
}

namespace {

// Tokenize a row by comma. Returns the number of tokens written into out;
// caller passes the capacity. Trailing CR/LF is stripped from the last token.
int TokenizeRow(std::string_view line, std::string_view* out, int capacity)
{
    int count = 0;
    size_t pos = 0;
    while (pos <= line.size() && count < capacity) {
        size_t comma = line.find(',', pos);
        std::string_view tok =
            (comma == std::string_view::npos)
                ? line.substr(pos)
                : line.substr(pos, comma - pos);
        if (count + 1 == capacity || comma == std::string_view::npos)
            tok = RstripCrLf(tok);
        out[count++] = tok;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return count;
}

bool ParseFloatTok(std::string_view tok, float& out)
{
    if (tok.empty()) return false;
    char buf[64];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* end = nullptr;
    float v = std::strtof(buf, &end);
    if (end == buf) return false;
    out = v;
    return true;
}

bool ParseDoubleTok(std::string_view tok, double& out)
{
    if (tok.empty()) return false;
    char buf[64];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* end = nullptr;
    double v = std::strtod(buf, &end);
    if (end == buf) return false;
    out = v;
    return true;
}

bool ParseIntTok(std::string_view tok, int& out)
{
    if (tok.empty()) return false;
    char buf[32];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* end = nullptr;
    long v = std::strtol(buf, &end, 10);
    if (end == buf) return false;
    out = (int)v;
    return true;
}

bool ParseUint32Tok(std::string_view tok, uint32_t& out)
{
    if (tok.empty()) return false;
    char buf[32];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* end = nullptr;
    unsigned long v = std::strtoul(buf, &end, 10);
    if (end == buf) return false;
    out = (uint32_t)v;
    return true;
}

bool ParseUint16Tok(std::string_view tok, uint16_t& out)
{
    uint32_t v32 = 0;
    if (!ParseUint32Tok(tok, v32)) return false;
    if (v32 > 0xFFFFu) return false;
    out = (uint16_t)v32;
    return true;
}

// Convenience: pull a field by index. Returns false on missing token,
// empty token, or numeric parse failure (consistent with LogCsv::ParseRow).
// If the column is absent in the log (idx == -1), leaves the destination
// unchanged and returns true.
bool TakeFloat(const std::string_view* tokens, int tokenCount, int idx, float& out)
{
    if (idx < 0) return true;
    if (idx >= tokenCount) return false;
    return ParseFloatTok(tokens[idx], out);
}
bool TakeDouble(const std::string_view* tokens, int tokenCount, int idx, double& out)
{
    if (idx < 0) return true;
    if (idx >= tokenCount) return false;
    return ParseDoubleTok(tokens[idx], out);
}
bool TakeInt(const std::string_view* tokens, int tokenCount, int idx, int& out)
{
    if (idx < 0) return true;
    if (idx >= tokenCount) return false;
    return ParseIntTok(tokens[idx], out);
}
bool TakeUint32(const std::string_view* tokens, int tokenCount, int idx, uint32_t& out)
{
    if (idx < 0) return true;
    if (idx >= tokenCount) return false;
    return ParseUint32Tok(tokens[idx], out);
}
bool TakeUint16(const std::string_view* tokens, int tokenCount, int idx, uint16_t& out)
{
    if (idx < 0) return true;
    if (idx >= tokenCount) return false;
    return ParseUint16Tok(tokens[idx], out);
}
// Copy an indexed token into a fixed-size char buffer, NUL-terminated.
// An empty token writes a 0-length string with NUL terminator and returns
// true (matches LogCsv::ParseRow's ParseString). The vnTimeUtc field is
// emitted as the empty string before the VN-300 first delivers a UTC
// fix; rejecting empty would drop those early rows.
bool TakeString(const std::string_view* tokens, int tokenCount, int idx,
                char* dst, size_t dstCapacity)
{
    if (idx < 0) return true;
    if (idx >= tokenCount) return false;
    if (dstCapacity == 0) return false;
    std::string_view tok = tokens[idx];
    size_t n = tok.size();
    if (n > dstCapacity - 1) n = dstCapacity - 1;
    std::memcpy(dst, tok.data(), n);
    dst[n] = '\0';
    return true;
}

}  // namespace

bool ParseRowByIndex(std::string_view line,
                     const HeaderIndex& idx,
                     onspeed::LogRow& row)
{
    std::string_view tokens[kHeaderIndexMaxColumns];
    int tokenCount = TokenizeRow(line, tokens, kHeaderIndexMaxColumns);
    if (tokenCount < idx.totalColumns) return false;

    // Always-present core columns.
    if (!TakeUint32(tokens, tokenCount, idx.idxTimeStampMs,  row.timeStampMs))     return false;
    if (!TakeInt   (tokens, tokenCount, idx.idxPfwd,         row.pfwdCounts))      return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxPfwdSmoothed, row.pfwdSmoothed))    return false;
    if (!TakeInt   (tokens, tokenCount, idx.idxP45,          row.p45Counts))       return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxP45Smoothed,  row.p45Smoothed))     return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxPStatic,      row.pStaticMbar))     return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxPaltFt,       row.paltFt))          return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxIasKt,        row.iasKt))           return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxAoaDeg,       row.angleOfAttackDeg))return false;
    if (!TakeInt   (tokens, tokenCount, idx.idxFlapsPos,     row.flapsPos))        return false;
    if (!TakeInt   (tokens, tokenCount, idx.idxDataMark,     row.dataMark))        return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxOatCelsius,   row.oatCelsius))      return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxTasKt,        row.tasKt))           return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxImuTemp,      row.imuTempCelsius))  return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxVerticalG,    row.imuVerticalG))    return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxLateralG,     row.imuLateralG))     return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxForwardG,     row.imuForwardG))     return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxRollRate,     row.imuRollRateDps))  return false;

    // PitchRate sign-flip recovery (issue #182). The CSV PitchRate column
    // stores -imuPitchRateDps; negate on parse to restore the raw value.
    if (idx.idxPitchRate >= 0) {
        float pitchRateCsv = 0.0f;
        if (!TakeFloat(tokens, tokenCount, idx.idxPitchRate, pitchRateCsv)) return false;
        row.imuPitchRateDps = -pitchRateCsv;
    }

    if (!TakeFloat (tokens, tokenCount, idx.idxYawRate,      row.imuYawRateDps))   return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxPitchDeg,     row.pitchDeg))        return false;
    if (!TakeFloat (tokens, tokenCount, idx.idxRollDeg,      row.rollDeg))         return false;

    // Boom (skipped via idx == -1 if not enabled).
    if (idx.boomEnabled) {
        if (!TakeFloat(tokens, tokenCount, idx.idxBoomStatic,  row.boomStatic))  return false;
        if (!TakeFloat(tokens, tokenCount, idx.idxBoomDynamic, row.boomDynamic)) return false;
        if (!TakeFloat(tokens, tokenCount, idx.idxBoomAlpha,   row.boomAlpha))   return false;
        if (!TakeFloat(tokens, tokenCount, idx.idxBoomBeta,    row.boomBeta))    return false;
        if (!TakeFloat(tokens, tokenCount, idx.idxBoomIas,     row.boomIasKt))   return false;
        if (!TakeInt  (tokens, tokenCount, idx.idxBoomAge,     row.boomAgeMs))   return false;
    }

    // EFIS — VN-300 set takes precedence over standard set (mutually exclusive
    // by HeaderIndex construction; both flags can't be true at once).
    if (idx.efisEnabled && idx.efisIsVn300) {
        if (!TakeFloat (tokens, tokenCount, idx.idxVnAngularRateRoll,  row.vnAngularRateRoll))  return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnAngularRatePitch, row.vnAngularRatePitch)) return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnAngularRateYaw,   row.vnAngularRateYaw))   return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnVelNedNorth,      row.vnVelNedNorth))      return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnVelNedEast,       row.vnVelNedEast))       return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnVelNedDown,       row.vnVelNedDown))       return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnAccelFwd,         row.vnAccelFwd))         return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnAccelLat,         row.vnAccelLat))         return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnAccelVert,        row.vnAccelVert))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnYaw,              row.vnYawDeg))           return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnPitch,            row.vnPitchDeg))         return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnRoll,             row.vnRollDeg))          return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnLinAccFwd,        row.vnLinAccFwd))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnLinAccLat,        row.vnLinAccLat))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnLinAccVert,       row.vnLinAccVert))       return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnYawSigma,         row.vnYawSigma))         return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnRollSigma,        row.vnRollSigma))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnPitchSigma,       row.vnPitchSigma))       return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnGnssVelNedNorth,  row.vnGnssVelNedNorth))  return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnGnssVelNedEast,   row.vnGnssVelNedEast))   return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxVnGnssVelNedDown,   row.vnGnssVelNedDown))   return false;
        if (!TakeDouble(tokens, tokenCount, idx.idxVnGnssLat,          row.vnGnssLat))          return false;
        if (!TakeDouble(tokens, tokenCount, idx.idxVnGnssLon,          row.vnGnssLon))          return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxVnGpsFix,           row.vnGpsFix))           return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxVnDataAge,          row.vnDataAgeMs))        return false;
        if (!TakeString(tokens, tokenCount, idx.idxVnTimeUtc,
                        row.vnTimeUtc, onspeed::kLogRowUtcTimeLen))                             return false;
    } else if (idx.efisEnabled) {
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisIas,           row.efisIasKt))           return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisPitch,         row.efisPitchDeg))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisRoll,          row.efisRollDeg))         return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisLateralG,      row.efisLateralG))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisVerticalG,     row.efisVerticalG))       return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisPercentLift,   row.efisPercentLift))     return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisPalt,          row.efisPaltFt))          return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisVsi,           row.efisVsiFpm))          return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisTas,           row.efisTasKt))           return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisOat,           row.efisOatCelsius))      return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisFuelRemaining, row.efisFuelRemaining))   return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisFuelFlow,      row.efisFuelFlow))        return false;
        if (!TakeFloat (tokens, tokenCount, idx.idxEfisMap,           row.efisMap))             return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisRpm,           row.efisRpm))             return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisPercentPower,  row.efisPercentPower))    return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisMagHeading,    row.efisMagHeading))      return false;
        if (!TakeInt   (tokens, tokenCount, idx.idxEfisAge,           row.efisAgeMs))           return false;
        if (!TakeUint32(tokens, tokenCount, idx.idxEfisTime,          row.efisTimestampMs))     return false;
    }

    // Always-present derived tail.
    if (!TakeFloat(tokens, tokenCount, idx.idxEarthVerticalG, row.earthVerticalG))  return false;
    if (!TakeFloat(tokens, tokenCount, idx.idxFlightPathDeg,  row.flightPathDeg))   return false;
    if (!TakeFloat(tokens, tokenCount, idx.idxVsiFpm,         row.vsiFpm))          return false;
    if (!TakeFloat(tokens, tokenCount, idx.idxAltitude,       row.altitudeFt))      return false;
    if (!TakeFloat(tokens, tokenCount, idx.idxDerivedAoa,     row.derivedAoaDeg))   return false;
    if (!TakeFloat(tokens, tokenCount, idx.idxCoeffP,         row.coeffP))          return false;

    // Tail-optional flapsRawADC (format version 2). Reflect presence into the
    // LogRow so downstream consumers can tell "absent / unknown" from "0".
    if (idx.idxFlapsRawAdc >= 0) {
        if (!TakeUint16(tokens, tokenCount, idx.idxFlapsRawAdc, row.flapsRawAdc))  return false;
        row.flapsRawAdcPresent = true;
    } else {
        row.flapsRawAdcPresent = false;
    }

    return true;
}

}  // namespace onspeed::proto::log_csv
