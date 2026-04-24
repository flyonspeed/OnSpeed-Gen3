// TestHelpers.h — shared glue for the renderer test fixtures.
//
// The UPDATE_GOLDEN workflow:
//   UPDATE_GOLDEN=1 pio test -e native -f test_display_renderers
//
// prints each fixture's observed coordHash() and does NOT assert on the
// goldens. Paste the printed values into goldens.h, re-run without the
// env var to confirm a clean pass, commit both together.

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unity.h>

#include "RenderShim.h"

// True when tests should print hashes and skip assertions.
inline bool updateGoldenMode()
{
    const char* v = std::getenv("UPDATE_GOLDEN");
    return v && *v && std::strcmp(v, "0") != 0;
}

// Assert that the current draw-event trace hashes to `expected`, or
// (in update mode) print its observed value for pasting into goldens.h.
// `fixtureName` appears in the update-mode printout so the operator can
// match a hash to the correct #define.
inline void assertOrPrintGolden(const char* fixtureName, const char* expected)
{
    std::string observed = drawEvents().coordHash();
    if (updateGoldenMode()) {
        std::printf("\n[UPDATE_GOLDEN] %s = %s\n", fixtureName, observed.c_str());
        return;
    }
    if (observed != expected) {
        std::string msg = std::string("coordHash mismatch for ") + fixtureName
                        + " — got " + observed + ", expected " + expected
                        + " (run UPDATE_GOLDEN=1 to regenerate)";
        TEST_FAIL_MESSAGE(msg.c_str());
    }
}

#endif // TEST_HELPERS_H
