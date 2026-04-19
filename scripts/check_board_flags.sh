#!/bin/bash
#
# check_board_flags.sh
#
# Enforce the board-flag containment invariant:
# `HW_V4P` and `HW_V4B` (and other future board flags) must appear ONLY in
# `HardwareMap.h` files. Every other file uses constexpr topology flags
# from HardwareMap.h with `if constexpr (...)` instead of `#ifdef`.
#
# This is the mechanical proof that adding a new board (e.g. Gen2v4) requires
# only writing a new HardwareMap.h, not touching any other source file.
#
# Usage:
#   ./scripts/check_board_flags.sh           # check
#   ./scripts/check_board_flags.sh -v        # verbose
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
SKETCH_DIR="$REPO_ROOT/software/OnSpeed-Gen3-ESP32"

VERBOSE=0
if [ "$1" = "-v" ]; then
    VERBOSE=1
fi

# Files where HW_V4* references are allowed.
ALLOW_PATTERNS=(
    'HardwareMap\.h$'
)

# Pattern matching forbidden references in code (not in comments).
FORBIDDEN_PATTERN='\bHW_V4[A-Z]'

VIOLATIONS=0

while IFS= read -r -d '' FILE; do
    REL="${FILE#$REPO_ROOT/}"

    # Check if file is allowlisted.
    SKIP=0
    for ALLOW in "${ALLOW_PATTERNS[@]}"; do
        if [[ "$REL" =~ $ALLOW ]]; then
            SKIP=1
            break
        fi
    done

    if [ $SKIP -eq 1 ]; then
        if [ $VERBOSE -eq 1 ]; then
            echo "Allowed:  $REL"
        fi
        continue
    fi

    if [ $VERBOSE -eq 1 ]; then
        echo "Checking: $REL"
    fi

    # Strip line comments before matching.
    STRIPPED=$(sed -E 's|//.*$||' "$FILE")
    MATCHES=$(echo "$STRIPPED" | grep -nE "$FORBIDDEN_PATTERN" || true)
    if [ -n "$MATCHES" ]; then
        echo "FORBIDDEN board-flag reference in $REL:"
        echo "$MATCHES" | sed 's/^/  /'
        echo
        VIOLATIONS=$((VIOLATIONS + 1))
    fi
done < <(find "$SKETCH_DIR" \
              \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.ino' \) \
              -print0)

if [ $VIOLATIONS -gt 0 ]; then
    echo
    echo "✗ $VIOLATIONS file(s) reference HW_V4P / HW_V4B outside HardwareMap.h"
    echo "  The two-layer driver design (PR #179) requires that only HardwareMap.h"
    echo "  knows about board flags. Other files should read 'if constexpr' against"
    echo "  HardwareMap.h's topology constants (e.g. kHasExternalMcp3202)."
    echo "  Adding a new board should mean writing a new HardwareMap.h — not"
    echo "  editing any other source file."
    exit 1
fi

echo "✓ Board-flag containment check passed"
exit 0
