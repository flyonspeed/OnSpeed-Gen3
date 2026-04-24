// LogMetaFile.cpp

#include <log/LogMetaFile.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace onspeed::log {

// ---------------------------------------------------------------------------
// EfisType <-> string
// ---------------------------------------------------------------------------

const char* EfisTypeToString(EfisType t)
{
    switch (t) {
        case EfisType::Dynon:  return "dynon";
        case EfisType::Garmin: return "garmin";
        case EfisType::Mgl:    return "mgl";
        case EfisType::Vn300:  return "vn300";
        case EfisType::None:
        default:               return "none";
    }
}

EfisType EfisTypeFromString(const char* s)
{
    if (!s) return EfisType::None;
    if (!std::strcmp(s, "dynon"))  return EfisType::Dynon;
    if (!std::strcmp(s, "garmin")) return EfisType::Garmin;
    if (!std::strcmp(s, "mgl"))    return EfisType::Mgl;
    if (!std::strcmp(s, "vn300"))  return EfisType::Vn300;
    return EfisType::None;
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

// Append a printf-formatted line into `buf`. Returns true on success, false
// on buffer-full (in which case *used is unchanged).
static bool appendLine(char* buf, size_t bufLen, size_t* used,
                       const char* fmt, ...)
{
    if (*used >= bufLen) return false;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *used, bufLen - *used, fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    size_t nu = static_cast<size_t>(n);
    if (nu >= bufLen - *used) return false;   // truncated
    *used += nu;
    return true;
}

size_t WriteMetaFile(const LogMeta& meta, char* buf, size_t bufLen)
{
    if (bufLen == 0) return 0;
    buf[0] = '\0';

    size_t used = 0;
    bool ok = true;
    ok &= appendLine(buf, bufLen, &used, "meta_version=%u\n",       meta.metaVersion);
    ok &= appendLine(buf, bufLen, &used, "log_format_version=%d\n", meta.logFormatVersion);
    ok &= appendLine(buf, bufLen, &used, "firmware=%s\n",           meta.firmware);
    ok &= appendLine(buf, bufLen, &used, "firmware_sha=%s\n",       meta.firmwareSha);
    ok &= appendLine(buf, bufLen, &used, "duration_ms=%lu\n",       (unsigned long)meta.durationMs);
    ok &= appendLine(buf, bufLen, &used, "row_count=%lu\n",         (unsigned long)meta.rowCount);
    ok &= appendLine(buf, bufLen, &used, "max_ias_kt=%.1f\n",       meta.maxIasKt);
    ok &= appendLine(buf, bufLen, &used, "max_palt_ft=%.0f\n",      meta.maxPaltFt);
    ok &= appendLine(buf, bufLen, &used, "efis_type=%s\n",          EfisTypeToString(meta.efisType));
    ok &= appendLine(buf, bufLen, &used, "gps_fix_seen=%d\n",       meta.gpsFixSeen ? 1 : 0);

    if (meta.utcStart[0] != '\0')
        ok &= appendLine(buf, bufLen, &used, "utc_start=%s\n",      meta.utcStart);
    if (meta.timeOfDayStart[0] != '\0')
        ok &= appendLine(buf, bufLen, &used, "time_of_day_start=%s\n", meta.timeOfDayStart);

    if (!ok) {
        buf[0] = '\0';
        return 0;
    }
    return used;
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

// Safe string copy into a fixed char array: always NUL-terminates, truncates
// at dstLen - 1. `dst` assumed non-null; `src` may contain no NUL within
// srcLen bytes (we stop at srcLen).
static void safeCopy(char* dst, size_t dstLen, const char* src, size_t srcLen)
{
    if (dstLen == 0) return;
    size_t n = (srcLen < dstLen - 1) ? srcLen : dstLen - 1;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool parseUint32(std::string_view v, uint32_t* out)
{
    char tmp[16];
    if (v.size() >= sizeof(tmp)) return false;
    std::memcpy(tmp, v.data(), v.size());
    tmp[v.size()] = '\0';
    // strtoul accepts leading sign and wraps negatives into huge positives.
    // Reject negatives outright so a hand-edited or corrupt sidecar can't
    // turn "-1" into 2^32-1.
    if (tmp[0] == '-' || tmp[0] == '+') return false;
    char* end = nullptr;
    unsigned long n = std::strtoul(tmp, &end, 10);
    if (end == tmp) return false;
    *out = static_cast<uint32_t>(n);
    return true;
}

static bool parseInt(std::string_view v, int* out)
{
    char tmp[16];
    if (v.size() >= sizeof(tmp)) return false;
    std::memcpy(tmp, v.data(), v.size());
    tmp[v.size()] = '\0';
    char* end = nullptr;
    long n = std::strtol(tmp, &end, 10);
    if (end == tmp) return false;
    *out = static_cast<int>(n);
    return true;
}

static bool parseFloat(std::string_view v, float* out)
{
    char tmp[24];
    if (v.size() >= sizeof(tmp)) return false;
    std::memcpy(tmp, v.data(), v.size());
    tmp[v.size()] = '\0';
    char* end = nullptr;
    float n = std::strtof(tmp, &end);
    if (end == tmp) return false;
    *out = n;
    return true;
}

bool ParseMetaFile(std::string_view text, LogMeta* out)
{
    if (!out) return false;
    int recognised = 0;

    size_t i = 0;
    while (i < text.size()) {
        // Find end of line.
        size_t j = text.find('\n', i);
        if (j == std::string_view::npos) j = text.size();
        std::string_view line = text.substr(i, j - i);
        i = (j == text.size()) ? j : j + 1;

        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;

        std::string_view key = line.substr(0, eq);
        std::string_view val = line.substr(eq + 1);

        // NUL-terminated key lookup. Copy into small stack buffer.
        char keyBuf[32];
        if (key.size() >= sizeof(keyBuf)) continue;
        std::memcpy(keyBuf, key.data(), key.size());
        keyBuf[key.size()] = '\0';

        if (!std::strcmp(keyBuf, "meta_version")) {
            uint32_t v = 0;
            if (parseUint32(val, &v)) { out->metaVersion = static_cast<uint8_t>(v); recognised++; }
        } else if (!std::strcmp(keyBuf, "log_format_version")) {
            if (parseInt(val, &out->logFormatVersion)) recognised++;
        } else if (!std::strcmp(keyBuf, "firmware")) {
            safeCopy(out->firmware, sizeof(out->firmware), val.data(), val.size());
            recognised++;
        } else if (!std::strcmp(keyBuf, "firmware_sha")) {
            safeCopy(out->firmwareSha, sizeof(out->firmwareSha), val.data(), val.size());
            recognised++;
        } else if (!std::strcmp(keyBuf, "duration_ms")) {
            if (parseUint32(val, &out->durationMs)) recognised++;
        } else if (!std::strcmp(keyBuf, "row_count")) {
            if (parseUint32(val, &out->rowCount)) recognised++;
        } else if (!std::strcmp(keyBuf, "max_ias_kt")) {
            if (parseFloat(val, &out->maxIasKt)) recognised++;
        } else if (!std::strcmp(keyBuf, "max_palt_ft")) {
            if (parseFloat(val, &out->maxPaltFt)) recognised++;
        } else if (!std::strcmp(keyBuf, "efis_type")) {
            char tmp[16];
            if (val.size() < sizeof(tmp)) {
                std::memcpy(tmp, val.data(), val.size());
                tmp[val.size()] = '\0';
                out->efisType = EfisTypeFromString(tmp);
                recognised++;
            }
        } else if (!std::strcmp(keyBuf, "gps_fix_seen")) {
            int v = 0;
            if (parseInt(val, &v)) { out->gpsFixSeen = (v != 0); recognised++; }
        } else if (!std::strcmp(keyBuf, "utc_start")) {
            safeCopy(out->utcStart, sizeof(out->utcStart), val.data(), val.size());
            recognised++;
        } else if (!std::strcmp(keyBuf, "time_of_day_start")) {
            safeCopy(out->timeOfDayStart, sizeof(out->timeOfDayStart), val.data(), val.size());
            recognised++;
        }
        // Unknown keys silently ignored.
    }
    return recognised > 0;
}

} // namespace onspeed::log
