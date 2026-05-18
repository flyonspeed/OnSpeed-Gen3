"""Byte-for-byte parity between Python frame.py and the C++
BuildDisplayFrame encoder.

`tools/onspeed_py/frame.py` is hand-mirrored from
`onspeed_core/proto/DisplaySerial.cpp::BuildDisplayFrame` because the
two surfaces it serves — `tools/synth-record/` and
`tools/m5-replay/replay.py` — need to emit wire frames at per-frame
speed (microseconds); subprocessing the C++ encoder would cost
milliseconds.  This test is the drift-prevention mechanism: it pins
the Python encoder against a C++-produced 83-byte golden binary.

Regenerate the fixture after any change to
`onspeed_core/proto/DisplaySerial.{h,cpp}`:

    cd tools/onspeed_py/tests
    g++ -std=c++17 \\
        -I ../../../software/Libraries/onspeed_core/src \\
        gen_v424_fixture.cpp \\
        ../../../software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp \\
        -o gen_v424_fixture
    ./gen_v424_fixture fixtures/v424_golden.bin

If this test fails it means `frame.py` and the C++ encoder have
diverged.  Fix the Python encoder; do not regenerate the fixture to
match — the C++ side is the authoritative wire format.
"""
from __future__ import annotations

import pathlib

from onspeed_py.frame import (
    FRAME_LEN,
    PAYLOAD_LEN,
    WIRE_VERSION,
    Frame,
)


FIXTURE = pathlib.Path(__file__).parent / "fixtures" / "v424_golden.bin"


def test_constants_match_wire_v424() -> None:
    """Pin the size + version constants so casual edits to frame.py
    can't silently regress the wire."""
    assert PAYLOAD_LEN  == 79
    assert FRAME_LEN    == 83
    assert WIRE_VERSION == 24


def test_python_encoder_matches_cpp_golden() -> None:
    """Compare `Frame.to_bytes()` to the C++-produced 83-byte golden.

    Inputs must match those in `gen_v424_fixture.cpp` exactly.  A
    mismatch on any byte fails loudly with a byte-level diff so the
    next reader can localise the drift to a specific field's encoding.
    """
    f = Frame(
        pitch_deg=2.3,
        roll_deg=-1.5,
        ias_kts=147.0,
        ias_valid=True,
        palt_ft=8000,
        turnrate_dps=0.5,
        lateral_g=0.02,
        # vertical_g=1.0 → wire field 10 (×10).  The C++ side stores
        # verticalGScaled10 as a float (=10.0f); the Python encoder
        # multiplies by 10 internally.  Both yield the same int 10.
        vertical_g=1.0,
        percent_lift_pct=55.4,
        # vsi_fpm=200 → wire field 20 (vsiFpm/10).  C++ uses vsiFpm10
        # directly (=20).  Both yield the same int 20.
        vsi_fpm=200,
        oat_c=5,
        flightpath_deg=1.8,
        flap_deg=0,
        tones_on_pct_lift=63,
        onspeed_fast_pct_lift=70,
        onspeed_slow_pct_lift=80,
        stall_warn_pct_lift=95,
        flaps_min_deg=0,
        flaps_max_deg=30,
        g_onset_rate=0.0,
        spin_cue=0,
        data_mark=0,
        pip_pct_lift=63,
        # kOatRaw|kOatSat|kIas|kPalt|kTas|kDensityAlt = bits 0..5 = 0x3F.
        validity=0x003F,
    )
    expected = FIXTURE.read_bytes()
    assert len(expected) == FRAME_LEN, (
        f"fixture has {len(expected)} bytes, expected {FRAME_LEN}; "
        f"regenerate via gen_v424_fixture.cpp"
    )

    actual = f.to_bytes()
    assert len(actual) == FRAME_LEN

    if actual != expected:
        # Print both with offsets to aid manual diff.
        diff_bytes = []
        for i in range(FRAME_LEN):
            if actual[i] != expected[i]:
                diff_bytes.append(
                    f"@{i}: py={chr(actual[i])!r} cpp={chr(expected[i])!r}"
                )
        msg = (
            f"Python and C++ wire-bytes differ:\n"
            f"  expected: {expected!r}\n"
            f"  actual:   {actual!r}\n"
            f"  diffs:    {diff_bytes}"
        )
        raise AssertionError(msg)
