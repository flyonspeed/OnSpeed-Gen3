"""Shared host_main binary path + preflight."""
from pathlib import Path

# Repo-relative path to the build artifact produced by:
#   cd tools/regression && pio run -e native
HOST_MAIN = (
    Path(__file__).resolve().parents[2]
    / "tools" / "regression" / ".pio" / "build" / "native" / "program"
)


def require_host_main() -> Path:
    """Return the host_main binary path, or raise a helpful RuntimeError."""
    if HOST_MAIN.exists():
        return HOST_MAIN
    raise RuntimeError(
        f"host_main binary not found at {HOST_MAIN}. "
        f"Build it: cd tools/regression && pio run -e native"
    )
