#!/bin/bash
#
# check_core_purity.sh
#
# Enforce the onspeed_core platform-freeness invariant:
# onspeed_core must not depend on Arduino, FreeRTOS, ESP-IDF, or any other
# platform-specific API. Every file under software/Libraries/onspeed_core/
# must compile with plain g++ on the development host.
#
# Fails (exit 1) if any forbidden include or symbol is found.
#
# Usage:
#   ./scripts/check_core_purity.sh           # check all files
#   ./scripts/check_core_purity.sh -v        # verbose — show every check
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
CORE_DIR="$REPO_ROOT/software/Libraries/onspeed_core"

VERBOSE=0
if [ "$1" = "-v" ]; then
    VERBOSE=1
fi

if [ ! -d "$CORE_DIR" ]; then
    echo "ERROR: $CORE_DIR does not exist" >&2
    exit 2
fi

# Forbidden patterns, one per line. Each is an extended-regex.
# Whitespace around hits is tolerated; line comments are stripped before match.
FORBIDDEN_PATTERNS=(
    # Arduino framework
    '#include[[:space:]]+[<"]Arduino\.h[>"]'
    '#include[[:space:]]+[<"]HardwareSerial\.h[>"]'
    '#include[[:space:]]+[<"]SPI\.h[>"]'
    '#include[[:space:]]+[<"]Wire\.h[>"]'
    '#include[[:space:]]+[<"]WiFi\.h[>"]'
    '#include[[:space:]]+[<"]SdFat\.h[>"]'
    '#include[[:space:]]+[<"]FS\.h[>"]'
    '#include[[:space:]]+[<"]LittleFS\.h[>"]'
    '#include[[:space:]]+[<"]OneButton\.h[>"]'
    '#include[[:space:]]+[<"]OneWire\.h[>"]'

    # FreeRTOS
    '#include[[:space:]]+[<"]FreeRTOS\.h[>"]'
    '#include[[:space:]]+[<"]freertos/'
    '\bxTaskCreate(Pinned(ToCore)?)?\b'
    '\bxSemaphore(Take|Give|Create)'
    '\bvTaskDelay'

    # ESP-IDF
    '#include[[:space:]]+[<"]esp_'
    '#include[[:space:]]+[<"]soc/'
    '#include[[:space:]]+[<"]driver/'
    '\besp_reset_reason\b'
    '\besp_restart\b'
    '\bi2s_driver_install\b'

    # Arduino runtime functions (common reach-ins)
    '\bmillis\s*\('
    '\bmicros\s*\('
    '\bdelay\s*\('
    '\bdelayMicroseconds\s*\('
    '\bdigitalWrite\s*\('
    '\bdigitalRead\s*\('
    '\banalogRead\s*\('
    '\banalogWrite\s*\('
    '\bpinMode\s*\('
    '\bSerial\.'

    # Arduino-specific types
    '\bString\s+[a-zA-Z_]'
)

VIOLATIONS=0

# Walk every .cpp / .h / .hpp under CORE_DIR (but NOT library.json etc).
while IFS= read -r -d '' FILE; do
    REL="${FILE#$REPO_ROOT/}"
    if [ $VERBOSE -eq 1 ]; then
        echo "Checking: $REL"
    fi

    # Strip line comments for matching (block comments left in — conservative).
    STRIPPED=$(sed -E 's|//.*$||' "$FILE")

    for PATTERN in "${FORBIDDEN_PATTERNS[@]}"; do
        MATCHES=$(echo "$STRIPPED" | grep -nE "$PATTERN" || true)
        if [ -n "$MATCHES" ]; then
            echo "FORBIDDEN in $REL:"
            echo "  pattern: $PATTERN"
            echo "$MATCHES" | sed 's/^/  /'
            echo
            VIOLATIONS=$((VIOLATIONS + 1))
        fi
    done
done < <(find "$CORE_DIR" \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0)

if [ $VIOLATIONS -gt 0 ]; then
    echo
    echo "✗ $VIOLATIONS forbidden-pattern match(es) found in onspeed_core/"
    echo "  onspeed_core must remain platform-free (no Arduino, FreeRTOS, ESP-IDF, etc)."
    echo "  The boundary was established in PRs #178 and #179 — platform code belongs"
    echo "  in the sketch, pure logic belongs in onspeed_core/."
    exit 1
fi

echo "✓ onspeed_core purity check passed"
exit 0
