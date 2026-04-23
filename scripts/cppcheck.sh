#!/bin/bash
#
# Run cppcheck static analysis on project source files
#
# Usage: ./scripts/cppcheck.sh [--strict] [--target TARGET]
#   --strict:        Exit with error code on errors (not warnings/style)
#   --target TARGET: One of "main" (default), "m5", or "all"
#
# Targets:
#   main — software/OnSpeed-Gen3-ESP32 (ESP32 firmware for the Gen3 box)
#   m5   — software/OnSpeed-M5-Display/src (our M5 firmware — vendored
#          libraries under lib/ are NOT scanned)
#   all  — both of the above
#
# Install cppcheck:
#   macOS:  brew install cppcheck
#   Ubuntu: sudo apt-get install cppcheck
#

set -e

# Find repo root (directory containing this script's parent)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
MAIN_SRC_DIR="$REPO_ROOT/software/OnSpeed-Gen3-ESP32"
M5_SRC_DIR="$REPO_ROOT/software/OnSpeed-M5-Display/src"

# Check if cppcheck is installed
if ! command -v cppcheck &> /dev/null; then
    echo "Error: cppcheck not found. Install with:"
    echo "  macOS:  brew install cppcheck"
    echo "  Ubuntu: sudo apt-get install cppcheck"
    exit 1
fi

# Parse arguments
STRICT_MODE=0
TARGET="main"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --strict)
            STRICT_MODE=1
            shift
            ;;
        --target)
            TARGET="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1"
            echo "Usage: $0 [--strict] [--target main|m5|all]"
            exit 2
            ;;
    esac
done

# Shared cppcheck flags. See top-of-file comment for what each suppression
# is for.
COMMON_ARGS=(
    --language=c++
    --std=c++20
    --enable=warning,style,performance,portability
    --suppress=missingIncludeSystem
    --suppress=unusedFunction
    --suppress=uninitMemberVar
    --suppress=noExplicitConstructor
    --suppress=unusedStructMember
    --inline-suppr
)

if [[ $STRICT_MODE -eq 1 ]]; then
    # Only fail on actual errors, not warnings/style
    COMMON_ARGS+=(--error-exitcode=1)
fi

run_cppcheck() {
    local label="$1"
    local src_dir="$2"

    echo "Running cppcheck on $label ($src_dir)..."
    echo ""

    # Recursively collect all .cpp and .h files. Pre-PR-4.2 sketch root
    # contained .cpp/.h files at top level; post-PR-4.2 they all live
    # under src/ (a symlink to sketch_common/src/). A non-recursive
    # *.cpp glob would scan effectively nothing for the main firmware.
    # -L tells find to follow symlinks so it descends into src/.
    #
    # Exclude Audio/ and Web/ — these folders hold generated PCM byte
    # arrays and embedded HTML/JS/CSS strings. They're data-as-.h-file,
    # not real source; cppcheck output on them is noise.
    local files=()
    while IFS= read -r -d '' f; do
        files+=("$f")
    done < <(find -L "$src_dir" \
        \( -path "$src_dir/Audio" -o -path "$src_dir/Web" \) -prune -o \
        -type f \( -name "*.cpp" -o -name "*.h" \) -print0)

    cppcheck "${COMMON_ARGS[@]}" \
        -I "$src_dir" \
        "${files[@]}"

    echo ""
}

case "$TARGET" in
    main)
        run_cppcheck "main firmware" "$MAIN_SRC_DIR"
        ;;
    m5)
        run_cppcheck "M5 display firmware" "$M5_SRC_DIR"
        ;;
    all)
        run_cppcheck "main firmware" "$MAIN_SRC_DIR"
        run_cppcheck "M5 display firmware" "$M5_SRC_DIR"
        ;;
    *)
        echo "Unknown target: $TARGET (expected main, m5, or all)"
        exit 2
        ;;
esac

echo "cppcheck complete."
