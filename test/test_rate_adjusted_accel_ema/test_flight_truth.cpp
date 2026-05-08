// test_flight_truth.cpp — Flight-truth comparison for RateAdjustedAccelEma.
//
// STATUS: SKIPPED — test_flight_truth_rms_divergence is TEST_IGNORE'd
// pending Issue #485.
//
// Issue #485: Record a 208 Hz reference flight log for filter validation.
// Sam sets iLogRate=208 in config and flies a representative pattern (taxi,
// takeoff, climb, maneuvering turns, cruise, descent, landing). The resulting
// log file becomes the fixture at:
//   test/fixtures/flight_truth/n720ak_208hz_reference.csv
//
// Once that fixture exists:
//   1. Remove the TEST_IGNORE_MESSAGE call in test_flight_truth_rms_divergence.
//   2. Run the suite, observe the stderr RMS print, and update kPinnedRmsTol.
//   3. Commit the fixture file alongside this test change.
//
// The main entry point and setUp/tearDown live in main.cpp.
//
// ============================================================================
// What this test does (when un-skipped):
//
//   The test reads a 208 Hz CSV flight log, runs two filters on the raw
//   LateralG column, and compares their output:
//
//   REFERENCE (firmware-form):
//     - Apply EMA with α=kFirmwareAlpha and dt=1/208 Hz to every row.
//     - This mirrors what the AHRS engine actually computed during the flight
//       (see Ahrs.cpp accelLatFilter_ at kAccSmoothing=0.060899).
//     - This is what the M5 displayed via the wire protocol.
//
//   CANDIDATE (replay path):
//     - Decimate the 208 Hz input to 50 Hz (keep every kDecimation-th sample).
//     - Apply RateAdjustedAccelEma(50, kAccelEmaTauSec) to the decimated stream.
//     - This is what the LogReplayEngine produces from a 50 Hz log row.
//
//   COMPARISON:
//     - Sample-align: decimate the reference output to 50 Hz.
//     - Compute RMS divergence over the entire flight.
//     - Assert RMS ≤ kPinnedRmsTol (filled in once the fixture exists).
//
// For signals below 25 Hz (50 Hz Nyquist), the candidate converges to the
// reference. For signals between 25 Hz and 67 Hz (IMU analog LPF), the 50 Hz
// log has aliased content the filter cannot recover. The pinned tolerance
// captures both contributions.
// ============================================================================

#include <unity.h>
#include <filters/RateAdjustedAccelEma.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>

using onspeed::filters::RateAdjustedAccelEma;
using onspeed::filters::kAccelEmaTauSec;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr float kFirmwareAlpha = 0.060899f;
static constexpr int   kDecimation    = 4;      // 208 Hz / 50 Hz ≈ 4.16; approximate as 4
static constexpr float kLogHz         = 50.0f;

// Pinned RMS tolerance — to be measured and filled in once the fixture exists.
// Set to -1 (invalid) until real data establishes the bound; the test checks
// this and fails with an explanatory message rather than asserting a bogus 0.
static constexpr float kPinnedRmsTol = -1.0f;   // PLACEHOLDER — see Issue #485

// Fixture path (relative to repo root; resolved at compile time via ONSPEED_REPO_ROOT).
#ifdef ONSPEED_REPO_ROOT
static const char* kFixturePath =
    ONSPEED_REPO_ROOT "/test/fixtures/flight_truth/n720ak_208hz_reference.csv";
#else
static const char* kFixturePath =
    "test/fixtures/flight_truth/n720ak_208hz_reference.csv";
#endif

// ---------------------------------------------------------------------------
// CSV helpers
// ---------------------------------------------------------------------------

static int findColumn(const char* header, const char* name)
{
    int idx = 0;
    const char* p = header;
    while (*p != '\0' && *p != '\n') {
        const char* start = p;
        while (*p != ',' && *p != '\n' && *p != '\0') ++p;
        size_t len = static_cast<size_t>(p - start);
        if (std::strncmp(start, name, len) == 0 && std::strlen(name) == len) {
            return idx;
        }
        ++idx;
        if (*p == ',') ++p;
    }
    return -1;
}

static float parseColumn(const char* row, int col)
{
    int idx = 0;
    const char* p = row;
    while (*p != '\0' && *p != '\n') {
        if (idx == col) {
            char* end = nullptr;
            float val = strtof(p, &end);
            if (end == p) return std::numeric_limits<float>::quiet_NaN();
            return val;
        }
        while (*p != ',' && *p != '\n' && *p != '\0') ++p;
        ++idx;
        if (*p == ',') ++p;
    }
    return std::numeric_limits<float>::quiet_NaN();
}

// ---------------------------------------------------------------------------
// Flight-truth comparison test (IGNORED until Issue #485).
// ---------------------------------------------------------------------------

void test_flight_truth_rms_divergence()
{
    TEST_IGNORE_MESSAGE(
        "Skipped: fixture test/fixtures/flight_truth/n720ak_208hz_reference.csv "
        "does not exist yet. Issue #485 tracks recording the 208 Hz reference "
        "flight log. Once the fixture is committed, remove this TEST_IGNORE_MESSAGE, "
        "run the suite to observe the RMS divergence on stderr, and update "
        "kPinnedRmsTol in this file.");

    // -----------------------------------------------------------------------
    // The code below executes only when un-skipped. It is written and
    // compiler-verified, but dormant until the fixture exists.
    // -----------------------------------------------------------------------

    FILE* f = std::fopen(kFixturePath, "r");
    if (f == nullptr) {
        TEST_FAIL_MESSAGE("Cannot open 208 Hz reference fixture. See Issue #485.");
        return;
    }

    char header[4096] = {};
    if (std::fgets(header, sizeof(header), f) == nullptr) {
        std::fclose(f);
        TEST_FAIL_MESSAGE("Fixture file is empty.");
        return;
    }

    int ayCol = findColumn(header, "LateralG");
    if (ayCol < 0) {
        ayCol = findColumn(header, "Ay");
    }
    if (ayCol < 0) {
        std::fclose(f);
        TEST_FAIL_MESSAGE("Fixture CSV has no 'LateralG' or 'Ay' column.");
        return;
    }

    float ref_val  = 0.0f;
    bool  ref_init = false;

    RateAdjustedAccelEma candidate(kLogHz, kAccelEmaTauSec);

    double sumSqDiff = 0.0;
    int    nCompared = 0;
    int    sampleIdx = 0;

    char row[4096] = {};
    while (std::fgets(row, sizeof(row), f) != nullptr) {
        float ay = parseColumn(row, ayCol);
        if (std::isnan(ay)) { ++sampleIdx; continue; }

        // Reference: firmware-form EMA at 208 Hz (mirrors Ahrs.cpp).
        float ref_out;
        if (!ref_init) {
            ref_val  = ay;
            ref_init = true;
            ref_out  = ref_val;
        } else {
            ref_val = kFirmwareAlpha * ay + (1.0f - kFirmwareAlpha) * ref_val;
            ref_out = ref_val;
        }

        // Candidate: rate-adjusted EMA at 50 Hz on decimated input.
        if (sampleIdx % kDecimation == 0) {
            float cand_out = candidate.update(ay);
            double diff = static_cast<double>(ref_out - cand_out);
            sumSqDiff += diff * diff;
            ++nCompared;
        }

        ++sampleIdx;
    }
    std::fclose(f);

    if (nCompared < 100) {
        TEST_FAIL_MESSAGE("Fixture too short — fewer than 100 comparable samples.");
        return;
    }

    float rms = static_cast<float>(std::sqrt(sumSqDiff / nCompared));

    // Report for the PR author to pin the tolerance.
    std::fprintf(stderr,
                 "[flight_truth] RMS divergence = %.6f g  (nCompared=%d)\n",
                 rms, nCompared);

    if (kPinnedRmsTol < 0.0f) {
        TEST_FAIL_MESSAGE(
            "kPinnedRmsTol is the placeholder (-1). "
            "Read the RMS from stderr and set the constant.");
    } else {
        TEST_ASSERT_FLOAT_WITHIN(kPinnedRmsTol, 0.0f, rms);
    }
}
