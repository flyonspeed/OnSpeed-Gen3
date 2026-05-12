// ConsumeAlignedWrite.h
//
// Pure logic for consuming a staging buffer after a (possibly short)
// SD write. Pulled out of LogSensor.cpp so it can be unit-tested
// without SdFat / FreeRTOS dependencies.
//
// Contract: a caller fills `szBuf` up to `uBufUsed` bytes, decides to
// flush some byte-aligned prefix of length `uRequested` to SD, then
// receives back the actual count `uActual` that the SD layer accepted.
// This helper rearranges `szBuf` so the bytes still owed to SD live at
// the front (`[0, uTailRemaining)`) and the bytes that arrived in
// `szBuf` after the flush attempt follow them. The new `uBufUsed`
// is reported through the same out parameter.
//
// Short writes are surfaced rather than silently absorbed: callers
// inspect the return value, retry next iteration, and rate-limit a
// warning so the bug is visible in `g_Log` (and, after PR #N, the
// .dbg file).

#ifndef ONSPEED_CORE_LOG_CONSUME_ALIGNED_WRITE_H
#define ONSPEED_CORE_LOG_CONSUME_ALIGNED_WRITE_H

#include <cstddef>

namespace onspeed::log {

// Returns true when uActual == uRequested (all requested bytes landed
// on disk). Returns false on a short write; in both cases *uBufUsed
// is updated to the new occupied length of szBuf and the layout is:
//
//     [0, retryHead)            unwritten tail to retry next round
//     [retryHead, *uBufUsed)    bytes that were past the flush window
//                               at call time (untouched)
//
// where retryHead = uRequested - uActual.
//
// Preconditions (asserted in debug builds, defensively clamped in
// release):
//   - uRequested <= *uBufUsed
//   - uActual    <= uRequested
//   - szBuf points to a buffer of at least uBufLen bytes, with
//     uBufLen >= *uBufUsed.
//
// On a malformed call (uActual > uRequested), the buffer is left
// untouched and the function returns false. Callers should treat
// this as "SD layer is misbehaving" and log a warning.
bool ConsumeAlignedWrite(size_t  uRequested,
                         size_t  uActual,
                         char   *szBuf,
                         size_t  uBufLen,
                         size_t *uBufUsed);

}  // namespace onspeed::log

#endif  // ONSPEED_CORE_LOG_CONSUME_ALIGNED_WRITE_H
