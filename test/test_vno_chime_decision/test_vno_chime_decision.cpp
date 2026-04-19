// test_vno_chime_decision.cpp — Unit tests for VnoChimeDetector
//
// Covers: threshold boundaries (strict >), repeat-timeout debounce,
// Reset() behavior, and first-trigger edge cases.

#include <unity.h>
#include <audio/VnoChimeDecision.h>

using namespace onspeed::audio;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Standard config: Vno=150 kt, 3-second interval (matches firmware defaults).
static VnoChimeConfig makeDefaultConfig()
{
    VnoChimeConfig cfg;
    cfg.vnoKt            = 150.0f;
    cfg.repeatIntervalMs = 3000;
    return cfg;
}

// ---------------------------------------------------------------------------
// Test 1: below Vno → no trigger
// ---------------------------------------------------------------------------

void test_below_vno_no_trigger()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 149.9f;
    in.tickMs = 1000;

    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 2: exactly at Vno → no trigger (strictly above required)
// ---------------------------------------------------------------------------

void test_at_vno_no_trigger()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 150.0f;
    in.tickMs = 1000;

    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 3: strictly above Vno → trigger
// ---------------------------------------------------------------------------

void test_above_vno_triggers()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 150.1f;
    in.tickMs = 1000;

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 4: debounce suppresses rapid re-trigger
// ---------------------------------------------------------------------------

void test_debounce_suppresses_rapid_retrigger()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 155.0f;
    in.tickMs = 1000;

    // First trigger
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // Immediately after — 0 ms elapsed
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // 2999 ms later — still within 3000 ms timeout
    in.tickMs = 1000 + 2999;
    TEST_ASSERT_FALSE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 5: re-triggers after interval elapses
// ---------------------------------------------------------------------------

void test_retrigger_after_interval()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 155.0f;
    in.tickMs = 1000;

    // First trigger at t=1000
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // t=3999 — one ms before boundary: still suppressed
    in.tickMs = 3999;
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // t=4000 — exactly at boundary (elapsed==repeatIntervalMs): condition
    // elapsed < repeatIntervalMs is false → chime fires
    in.tickMs = 4000;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 6: Reset() clears debounce
// ---------------------------------------------------------------------------

void test_reset_clears_debounce()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 160.0f;
    in.tickMs = 1000;

    TEST_ASSERT_TRUE(det.Update(in, cfg));
    TEST_ASSERT_FALSE(det.Update(in, cfg));   // suppressed

    det.Reset();
    TEST_ASSERT_TRUE(det.Update(in, cfg));   // fires again after reset
}

// ---------------------------------------------------------------------------
// Test 7: first trigger at tickMs=0 (no spurious suppression)
// ---------------------------------------------------------------------------

void test_first_trigger_at_tick_zero()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 155.0f;
    in.tickMs = 0;

    // haveTriggered_=false on fresh instance → must trigger even at tickMs=0
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 8: configurable repeat interval is respected
// ---------------------------------------------------------------------------

void test_custom_interval_respected()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg;
    cfg.vnoKt            = 100.0f;
    cfg.repeatIntervalMs = 5000;   // 5 second interval

    VnoChimeInputs in;
    in.iasKt  = 110.0f;
    in.tickMs = 0;

    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // 4999 ms — inside 5s window → suppressed
    in.tickMs = 4999;
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // 5000 ms — exactly at boundary (elapsed==repeatIntervalMs) → fires
    in.tickMs = 5000;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 9: IAS drops below Vno during debounce, then rises above after timeout
// ---------------------------------------------------------------------------

void test_ias_oscillates_around_vno()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();
    VnoChimeInputs   in;
    in.iasKt  = 151.0f;
    in.tickMs = 0;

    // Trigger at t=0
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // Drop below Vno during debounce window
    in.iasKt  = 149.0f;
    in.tickMs = 1000;
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // Rise above Vno again after timeout
    in.iasKt  = 151.0f;
    in.tickMs = 3001;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 10: zero Vno config → triggers for any positive IAS
// ---------------------------------------------------------------------------

void test_zero_vno_triggers_for_any_positive_ias()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg;
    cfg.vnoKt            = 0.0f;
    cfg.repeatIntervalMs = 3000;

    VnoChimeInputs in;
    in.iasKt  = 0.1f;
    in.tickMs = 1000;

    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 11: repeatIntervalMs == 0 would fire every tick — callers must guard
//
// The detector does not defend against a zero interval: `elapsed < 0` on
// uint32_t is always false, so every over-Vno tick fires the chime.
// Housekeeping.cpp clamps g_Config.uVnoChimeInterval to at least 1 before
// converting to ms. This test pins that expectation — if anyone moves the
// guard into the detector later, this test fails and forces a conscious
// decision about where the invariant lives.
// ---------------------------------------------------------------------------

void test_zero_interval_fires_every_tick()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg;
    cfg.vnoKt            = 100.0f;
    cfg.repeatIntervalMs = 0;   // pathological — caller must clamp before here

    VnoChimeInputs in;
    in.iasKt = 110.0f;

    in.tickMs = 1000;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
    in.tickMs = 1001;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
    in.tickMs = 1002;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// Test 12: re-enabling bOverGWarning / bVnoChimeEnabled after it was off
//
// This detector has no concept of "enabled". Housekeeping.cpp gates whether
// Update() is called at all via g_Config.bVnoChimeEnabled. When the config
// is toggled off, Update() is skipped so lastTriggerMs_ freezes. When it
// comes back on, elapsed = now - lastTriggerMs_ is naturally large (seconds
// or more have passed), so the next over-Vno tick will fire immediately —
// which is the correct behavior: the pilot has been waiting, not chiming.
//
// The only edge case is toggling off and back on within the 3-second window
// of a just-fired chime. In that case the cooldown is still in effect.
// ---------------------------------------------------------------------------

void test_cooldown_outlives_skipped_ticks()
{
    VnoChimeDetector det;
    VnoChimeConfig   cfg = makeDefaultConfig();   // 150 kt, 3000 ms

    VnoChimeInputs in;
    in.iasKt = 160.0f;

    // Fire at t=1000
    in.tickMs = 1000;
    TEST_ASSERT_TRUE(det.Update(in, cfg));

    // Config toggled off → caller does not call Update() for 500 ms.
    // Config toggled back on at t=1500. Cooldown window (3000 ms from the
    // fire at t=1000) has not yet elapsed → no re-trigger.
    in.tickMs = 1500;
    TEST_ASSERT_FALSE(det.Update(in, cfg));

    // Config toggled off again, back on at t=4500. 3500 ms since fire →
    // cooldown expired → trigger allowed.
    in.tickMs = 4500;
    TEST_ASSERT_TRUE(det.Update(in, cfg));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int /*argc*/, char** /*argv*/)
{
    UNITY_BEGIN();

    RUN_TEST(test_below_vno_no_trigger);
    RUN_TEST(test_at_vno_no_trigger);
    RUN_TEST(test_above_vno_triggers);
    RUN_TEST(test_debounce_suppresses_rapid_retrigger);
    RUN_TEST(test_retrigger_after_interval);
    RUN_TEST(test_reset_clears_debounce);
    RUN_TEST(test_first_trigger_at_tick_zero);
    RUN_TEST(test_custom_interval_respected);
    RUN_TEST(test_ias_oscillates_around_vno);
    RUN_TEST(test_zero_vno_triggers_for_any_positive_ias);
    RUN_TEST(test_zero_interval_fires_every_tick);
    RUN_TEST(test_cooldown_outlives_skipped_ticks);

    return UNITY_END();
}
