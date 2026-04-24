// goldens.h
//
// Coord-hash goldens for the M5 display renderer test suite. Values are
// captured by running:
//
//    UPDATE_GOLDEN=1 pio test -e native -f test_display_renderers
//
// which, for each fixture, prints its observed coordHash() to stdout.
// Paste the new values in here when a deliberate layout change lands.
// Whenever this file is modified, also sanity-check the per-method call
// counts and anchor assertions in the corresponding test file — if
// those still pass but the hash changed, you captured a real structural
// change that the per-call-type counts happened not to notice, and a
// reviewer should look at it.
//
// The hash is FNV-1a over the (method, args...) sequence. Small local
// edits (one-pixel shifts, color swaps) surface as an ~8-char diff
// here — intentional and informative.

#ifndef TEST_DISPLAY_GOLDENS_H
#define TEST_DISPLAY_GOLDENS_H

// Baseline captured 2026-04-24 against branch huvver-display-integration-spec
// with `sin/cos` still double-precision (PR 2 will switch to sinf/cosf and
// require a regen).

// Each golden corresponds to one renderer invocation in a known input
// state. Tests set the same inputs; mismatch means a real change.

#define GOLDEN_displayAOA                "0xbd02a58c"
#define GOLDEN_displayAOA_numericless    "0x6340cba6"
#define GOLDEN_attitude_level            "0xa30e3c25"
#define GOLDEN_attitude_banked           "0x5ee74241"
#define GOLDEN_decel_gauge_zero          "0x6b05c298"
#define GOLDEN_decel_gauge_negative      "0x533be791"
#define GOLDEN_gload_history_flat        "0xd0f4f3ce"
#define GOLDEN_gload_history_varied      "0x18d5219e"
#define GOLDEN_splash_screen             "0x06545ffe"

#endif // TEST_DISPLAY_GOLDENS_H
