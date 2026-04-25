// M5Unified.h shim — only on the include path for [env:huvver-avi]. Redirects
// the unconditional `#include <M5Unified.h>` in main.cpp / SerialRead.cpp to
// our TFT_eSPI compat shim instead of pulling in real M5Unified (which we
// don't link).

#pragma once
#include "../HuvverShim.h"
