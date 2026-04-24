// LogMetaFile.h
//
// Pure serialization functions for the LogMeta sidecar format. No I/O —
// callers handle read/write against SD. Format is plain `key=value\n`
// text, one pair per line. Unknown keys are ignored on parse.

#ifndef ONSPEED_CORE_LOG_LOG_META_FILE_H
#define ONSPEED_CORE_LOG_LOG_META_FILE_H

#include <cstddef>
#include <string_view>

#include <log/LogMeta.h>

namespace onspeed::log {

// Serialize `meta` into `buf`. Returns bytes written (not including NUL)
// on success, 0 on buffer-too-small. `buf[0]` is set to NUL on failure.
size_t WriteMetaFile(const LogMeta& meta, char* buf, size_t bufLen);

// Parse `text` as a LogMeta sidecar. Unknown keys are ignored; missing
// keys leave the corresponding LogMeta field at its LogMeta{} default.
// Returns true if at least one recognised key was parsed; false if the
// input yielded no recognised keys at all (empty string, all-garbage).
bool ParseMetaFile(std::string_view text, LogMeta* out);

} // namespace onspeed::log

#endif
