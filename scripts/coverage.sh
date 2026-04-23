#!/usr/bin/env bash
# coverage.sh — run the CI coverage job locally via Docker.
#
# Produces coverage.info and coverage-report/ in the repo root, matching
# what .github/workflows/ci.yml generates. The Docker image (defined in
# Dockerfile.coverage) mirrors the ubuntu-latest CI environment, so
# coverage numbers match bit-for-bit.
#
# Usage:
#   ./scripts/coverage.sh              Run coverage, produce report
#   ./scripts/coverage.sh --rebuild    Rebuild image from scratch (no cache)
#   ./scripts/coverage.sh --shell      Drop into a shell in the image for debug
#
# Open the report: open coverage-report/index.html  (macOS)
#                  xdg-open coverage-report/index.html  (Linux)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

IMAGE_TAG="onspeed-coverage:local"
DOCKERFILE="Dockerfile.coverage"

# --- Guards ----------------------------------------------------------------

if ! command -v docker >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: `docker` not found on PATH.

Install Docker Desktop (macOS/Windows) or Docker Engine (Linux), then
re-run this script. See https://docs.docker.com/get-docker/.

If you'd rather run coverage without Docker, you can install the GNU
toolchain via Homebrew on macOS (`brew install gcc lcov`) and run
`pio test -e native-coverage` with CC=gcc-15 CXX=g++-15; note that
coverage numbers may drift slightly from CI due to GCC version skew.
EOF
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    echo "error: \`docker\` command exists but daemon is not reachable." >&2
    echo "Start Docker Desktop / the Docker engine and re-run." >&2
    exit 1
fi

# --- Argument parsing ------------------------------------------------------

REBUILD=0
SHELL_MODE=0
for arg in "$@"; do
    case "$arg" in
        --rebuild) REBUILD=1 ;;
        --shell)   SHELL_MODE=1 ;;
        -h|--help)
            sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $arg" >&2
            exit 2
            ;;
    esac
done

# --- Build image -----------------------------------------------------------

BUILD_ARGS=()
if [ "$REBUILD" -eq 1 ]; then
    BUILD_ARGS+=(--no-cache)
fi

echo "==> Building coverage image ($IMAGE_TAG)"
docker build "${BUILD_ARGS[@]}" -f "$DOCKERFILE" -t "$IMAGE_TAG" .

# --- Run -------------------------------------------------------------------

# Host UID/GID so generated files (coverage-report/, .pio/) aren't owned by
# root on the host. Docker Desktop on macOS abstracts this over its VM, but
# passing --user is still the right pattern for Linux hosts.
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

# :cached mount on macOS significantly speeds up file-heavy workloads
# (PIO reads thousands of framework files each run). No-op on Linux.
MOUNT_FLAG=""
if [ "$(uname)" = "Darwin" ]; then
    MOUNT_FLAG=":cached"
fi

RUN_ARGS=(
    run --rm
    -v "$REPO_ROOT:/workspace${MOUNT_FLAG}"
    -w /workspace
    --user "${HOST_UID}:${HOST_GID}"
    # PIO inside the container wants a writable home for its venv cache; the
    # default /root is not writable under a non-root --user, so redirect.
    -e HOME=/tmp
    -e PLATFORMIO_CORE_DIR=/tmp/.platformio
)

if [ "$SHELL_MODE" -eq 1 ]; then
    exec docker "${RUN_ARGS[@]}" -it "$IMAGE_TAG" bash
fi

echo "==> Running coverage"
docker "${RUN_ARGS[@]}" "$IMAGE_TAG"

echo
echo "==> Done."
echo "    HTML report: coverage-report/index.html"
echo "    lcov file:   coverage.info"
if [ "$(uname)" = "Darwin" ]; then
    echo "    Open with:   open coverage-report/index.html"
else
    echo "    Open with:   xdg-open coverage-report/index.html"
fi
