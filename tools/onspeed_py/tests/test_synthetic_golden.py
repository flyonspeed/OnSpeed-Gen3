"""Golden-bytes regression: verify the m5-replay synthetic stream still
produces byte-for-byte identical frames after the migration to
`onspeed_py`.

The hash was captured from `tools/m5-replay/replay.py` BEFORE the
import refactor. If a migration accidentally changes the output, the
test fails loudly. If the wire format intentionally changes (e.g. PR
#336 lands and adds `pip_pct_lift`), regenerate the hash by running
`generate_golden()` below and pasting the new value.
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

# Ensure we can import the m5-replay module regardless of where the
# tests are invoked from. m5-replay imports pyserial at module level;
# this test only exercises `synthetic_stream`, but the import still
# fires, so we skip cleanly if pyserial isn't on the path.
TOOLS_DIR = Path(__file__).resolve().parent.parent.parent
M5_REPLAY_DIR = TOOLS_DIR / "m5-replay"
sys.path.insert(0, str(M5_REPLAY_DIR))

from onspeed_py.config import FlapSetpoints

# Pinned pre-migration hash. See module docstring for regeneration.
GOLDEN_SHA256 = "d29e44dc48d9f0920afa6b6786dd032f5372cb21d67e61d173f5bd87b0faec3a"
GOLDEN_FRAME_COUNT = 100


def _make_setpoints() -> dict[int, FlapSetpoints]:
    """Two flap detents — fixed values, no file I/O. The synthetic
    scenario only exercises flaps 0 and 16."""
    return {
        0: FlapSetpoints(
            degrees=0,
            ldmax_aoa=2.0,
            onspeed_fast_aoa=5.0,
            onspeed_slow_aoa=7.5,
            stallwarn_aoa=9.5,
            alpha_0=-2.5,
            alpha_stall=11.0,
        ),
        16: FlapSetpoints(
            degrees=16,
            ldmax_aoa=1.5,
            onspeed_fast_aoa=4.0,
            onspeed_slow_aoa=6.5,
            stallwarn_aoa=9.0,
            alpha_0=-3.0,
            alpha_stall=10.5,
        ),
    }


def test_synthetic_stream_first_100_frames_match_golden() -> None:
    try:
        import replay  # m5-replay's replay.py
    except ImportError as e:
        if "serial" in str(e):
            import pytest
            pytest.skip("pyserial not installed; m5-replay import deferred")
        raise

    setpoints = _make_setpoints()
    gen = replay.synthetic_stream(setpoints)
    blob = b"".join(next(gen).to_bytes() for _ in range(GOLDEN_FRAME_COUNT))
    actual = hashlib.sha256(blob).hexdigest()
    assert actual == GOLDEN_SHA256, (
        f"synthetic_stream output drift! "
        f"expected={GOLDEN_SHA256} actual={actual}\n"
        f"If the wire format intentionally changed, regenerate the "
        f"hash and update GOLDEN_SHA256."
    )


def generate_golden() -> str:
    """Helper for regenerating GOLDEN_SHA256 after an intentional
    format change. Call from a Python REPL or one-off script."""
    import replay  # type: ignore

    setpoints = _make_setpoints()
    gen = replay.synthetic_stream(setpoints)
    blob = b"".join(next(gen).to_bytes() for _ in range(GOLDEN_FRAME_COUNT))
    return hashlib.sha256(blob).hexdigest()
