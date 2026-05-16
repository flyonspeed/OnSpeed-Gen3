// ConsumeAlignedWrite.cpp

#include <log/ConsumeAlignedWrite.h>

#include <cstring>

namespace onspeed::log {

bool ConsumeAlignedWrite(size_t  uRequested,
                         size_t  uActual,
                         char   *szBuf,
                         size_t  uBufLen,
                         size_t *uBufUsed)
{
    if (szBuf == nullptr || uBufUsed == nullptr)
        return false;

    // Malformed: SD layer claimed to write more than asked. Refuse to
    // touch the buffer; let the caller log and recover.
    if (uActual > uRequested)
        return false;

    // Clamp uRequested into the buffer to be defensive against caller
    // bugs (uRequested > uBufUsed). After clamp, uRequested <= *uBufUsed
    // <= uBufLen, so all arithmetic below is safe.
    if (uRequested > *uBufUsed)
        uRequested = *uBufUsed;
    if (uRequested > uBufLen)
        uRequested = uBufLen;

    const size_t uTail        = *uBufUsed - uRequested;  // bytes past flush window
    const size_t uRetry       = uRequested - uActual;    // unwritten head to retry

    // Fast path: full write. Shift any post-flush tail to the front.
    if (uRetry == 0)
    {
        if (uTail > 0 && uRequested > 0)
            std::memmove(szBuf, szBuf + uRequested, uTail);
        *uBufUsed = uTail;
        return true;
    }

    // Short write. Layout becomes:
    //   [0,            uRetry)          unwritten head (already at szBuf+uActual)
    //   [uRetry,       uRetry + uTail)  post-flush tail (already at szBuf+uRequested)
    //
    // Move the unwritten head down to position 0 first, then the tail
    // up to follow it. memmove handles overlap.
    std::memmove(szBuf,           szBuf + uActual,    uRetry);
    if (uTail > 0)
        std::memmove(szBuf + uRetry, szBuf + uRequested, uTail);

    *uBufUsed = uRetry + uTail;
    return false;
}

}  // namespace onspeed::log
