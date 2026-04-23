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
lcov --capture \
     --directory .pio/build/native-coverage \
     --output-file coverage.info \
     --rc branch_coverage=1

echo "==> Filtering test + system paths out of the report"
lcov --remove coverage.info \
     '*/test/*' \
     '*/.pio/*' \
     '/usr/*' \
     --output-file coverage.info \
     --rc branch_coverage=1 \
     --ignore-errors unused

echo "==> Generating HTML report"
genhtml coverage.info \
        --output-directory coverage-report \
        --rc branch_coverage=1

echo "==> Coverage summary"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "## Code Coverage"
        echo '```'
        lcov --summary coverage.info 2>&1
        echo '```'
    } >> "$GITHUB_STEP_SUMMARY"
fi
lcov --summary coverage.info
