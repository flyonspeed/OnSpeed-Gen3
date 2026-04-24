// LogMetaBuilder.cpp

#include <log/LogMetaBuilder.h>

#include <cstring>

namespace onspeed::log {

void LogMetaBuilder::Begin(const char* firmware,
                           const char* firmwareSha,
                           int         logFormatVersion,
                           EfisType    efisType)
{
    m_meta = LogMeta{};   // reset
    if (firmware) {
        std::strncpy(m_meta.firmware, firmware, sizeof(m_meta.firmware) - 1);
        m_meta.firmware[sizeof(m_meta.firmware) - 1] = '\0';
    }
    if (firmwareSha) {
        std::strncpy(m_meta.firmwareSha, firmwareSha, sizeof(m_meta.firmwareSha) - 1);
        m_meta.firmwareSha[sizeof(m_meta.firmwareSha) - 1] = '\0';
    }
    m_meta.logFormatVersion = logFormatVersion;
    m_meta.efisType         = efisType;
    m_firstTimeMs  = 0;
    m_lastTimeMs   = 0;
    m_haveFirstRow = false;
}

void LogMetaBuilder::OnRow(const onspeed::LogRow& row,
                           const char* hmsOrNull,
                           const char* utcOrNull)
{
    if (!m_haveFirstRow) {
        m_firstTimeMs  = row.timeStampMs;
        m_haveFirstRow = true;
    }
    m_lastTimeMs = row.timeStampMs;
    m_meta.rowCount++;

    if (row.iasKt  > m_meta.maxIasKt)  m_meta.maxIasKt  = row.iasKt;
    if (row.paltFt > m_meta.maxPaltFt) m_meta.maxPaltFt = row.paltFt;

    const bool haveHms = (hmsOrNull && hmsOrNull[0] != '\0');
    const bool haveUtc = (utcOrNull && utcOrNull[0] != '\0');

    if (haveHms || haveUtc)
        m_meta.gpsFixSeen = true;

    if (haveHms && m_meta.timeOfDayStart[0] == '\0') {
        std::strncpy(m_meta.timeOfDayStart, hmsOrNull,
                     sizeof(m_meta.timeOfDayStart) - 1);
        m_meta.timeOfDayStart[sizeof(m_meta.timeOfDayStart) - 1] = '\0';
    }
    if (haveUtc && m_meta.utcStart[0] == '\0') {
        std::strncpy(m_meta.utcStart, utcOrNull,
                     sizeof(m_meta.utcStart) - 1);
        m_meta.utcStart[sizeof(m_meta.utcStart) - 1] = '\0';
    }
}

LogMeta LogMetaBuilder::Finalize() const
{
    LogMeta out = m_meta;
    out.durationMs = m_haveFirstRow ? (m_lastTimeMs - m_firstTimeMs) : 0u;
    return out;
}

void LogMetaBuilder::Reset()
{
    m_meta = LogMeta{};
    m_firstTimeMs  = 0;
    m_lastTimeMs   = 0;
    m_haveFirstRow = false;
}

} // namespace onspeed::log
