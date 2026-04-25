// HuvverShim.cpp — definitions of globals declared in HuvverShim.h.
// Only built in the [env:huvver-avi] environment.

#if defined(HUVVER)
#include "HuvverShim.h"

HuvverDisplay g_huvverDisplay;
M5_t          M5;

// fonts:: -> global ::FreeSans12pt7b reference bindings. TFT_eSPI declares
// each font table as a global const GFXfont; we expose it under the
// M5GFX-style fonts:: namespace so the FSS*/FM* macros (which expand to
// &fonts::FreeSansXXpt7b) resolve.
namespace fonts
{
    const GFXfont& FreeMono9pt7b       = ::FreeMono9pt7b;
    const GFXfont& FreeMono12pt7b      = ::FreeMono12pt7b;
    const GFXfont& FreeMono18pt7b      = ::FreeMono18pt7b;
    const GFXfont& FreeMono24pt7b      = ::FreeMono24pt7b;
    const GFXfont& FreeMonoBold9pt7b   = ::FreeMonoBold9pt7b;
    const GFXfont& FreeMonoBold12pt7b  = ::FreeMonoBold12pt7b;
    const GFXfont& FreeMonoBold18pt7b  = ::FreeMonoBold18pt7b;
    const GFXfont& FreeMonoBold24pt7b  = ::FreeMonoBold24pt7b;
    const GFXfont& FreeSans9pt7b       = ::FreeSans9pt7b;
    const GFXfont& FreeSans12pt7b      = ::FreeSans12pt7b;
    const GFXfont& FreeSans18pt7b      = ::FreeSans18pt7b;
    const GFXfont& FreeSans24pt7b      = ::FreeSans24pt7b;
    const GFXfont& FreeSansBold9pt7b   = ::FreeSansBold9pt7b;
    const GFXfont& FreeSansBold12pt7b  = ::FreeSansBold12pt7b;
    const GFXfont& FreeSansBold18pt7b  = ::FreeSansBold18pt7b;
    const GFXfont& FreeSansBold24pt7b  = ::FreeSansBold24pt7b;
}
#endif
