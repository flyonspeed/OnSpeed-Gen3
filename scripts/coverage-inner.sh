#!/usr/bin/env bash
# coverage-inner.sh — run the native-coverage test suite, capture gcov data,
# filter, and produce an HTML report.
#
# Runs inside the coverage Docker image (scripts/coverage.sh) or directly on
# Linux CI (.github/workflows/ci.yml). Both call this script so the coverage
# numbers reported locally match CI bit-for-bit.
#
# Outputs (relative to repo root):
#   coverage.info           lcov tracefile (for Codecov upload / re-genhtml)
#   coverage-report/        standalone HTML report (open index.html)
#
# Exit status:
#   0 on success, non-zero if any step fails.
#
# Environment variables:
#   GITHUB_STEP_SUMMARY     (optional) when set, append markdown summary

set -euo pipefail

# Run from repo root regardless of cwd.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "==> Running native-coverage test suite"
pio test -e native-coverage -v

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "## Unit Test Results"
        echo "✅ All tests passed"
    } >> "$GITHUB_STEP_SUMMARY"
fi

echo "==> Capturing gcov data with lcov"
# Note on branch coverage: gcov counts a C++ exception-unwind edge on every
# heap-allocating call (std::string, std::vector, new). Those edges would
# only ever be covered by inducing std::bad_alloc in a test — not a
# meaningful target for a 32 MB-flash embedded system. We tried
# `--rc no_exception_branch=1` to suppress them but on our build it also
# strips most non-exception branches (9401 → 156), so the flag is off.
# Read the branch number in that light: ~60% is really ~85% on real
# control flow; the gap is allocator paranoia, not missing tests.
lcov --capture \
     --directory .pio/build/native-coverage \
     --output-file coverage.info \
     --rc branch_coverage=1

echo "==> Filtering test + system + vendored paths out of the report"
# Exclude tinyxml2: vendored library, pinned in PR #209. Testing upstream
# code is not our job — its inclusion dragged the headline number down by
# ~20 points with zero actionable signal.
lcov --remove coverage.info \
     '*/test/*' \
     '*/.pio/*' \
     '*/software/Libraries/tinyxml2/*' \
     '/usr/*' \
     --output-file coverage.info \
     --rc branch_coverage=1 \
     --ignore-errors unused

echo "==> Generating HTML report"
genhtml coverage.info \
        --output-directory coverage-report \
        --rc branch_coverage=1

echo "==> Coverage summary"
# lcov --summary in lcov 2.0 sometimes prints "no data found" for branches
# even when branch data is present in the tracefile. Fall back to direct
# parsing so our summary is always accurate.
SUMMARY="$(awk '
/^DA:/ { lf++; split($0, a, ","); if (a[2]+0 > 0) lh++ }
/^FNDA:/ { fnf++; split($0, a, ","); sub(/^FNDA:/, "", a[1]); if (a[1]+0 > 0) fnh++ }
/^BRDA:/ { bf++; n=split($0, a, ","); if (a[n] != "-" && a[n]+0 > 0) bh++ }
END {
    lp = (lf > 0) ? 100*lh/lf : 0
    fp = (fnf > 0) ? 100*fnh/fnf : 0
    bp = (bf > 0) ? 100*bh/bf : 0
    printf "  lines......: %5.1f%% (%d of %d lines)\n",       lp, lh,  lf
    printf "  functions..: %5.1f%% (%d of %d functions)\n",   fp, fnh, fnf
    printf "  branches...: %5.1f%% (%d of %d branches)\n",    bp, bh,  bf
}' coverage.info)"

echo "$SUMMARY"

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "## Code Coverage (onspeed_core only; vendored tinyxml2 excluded)"
        echo '```'
        echo "$SUMMARY"
        echo '```'
    } >> "$GITHUB_STEP_SUMMARY"
fi
