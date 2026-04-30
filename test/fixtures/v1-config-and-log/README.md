# V1 config + V1 log fixtures

Captured from one of Vac's calibration flights (late 2025) before the
config and log formats were modernized.  Used by the synth-record tool's
parser tests to ensure the Python `wire_frame_builder` and `from_log`
adapter both keep working against the historical formats.

## Files

- `vac_config.cfg` — V1 OnSpeed config XML.  Notable features:
  - Top-level `<SETPOINT_*>` lists (comma-separated per-flap values),
    not `<FLAP_POSITION>` blocks.
  - Tags like `<3DAUDIO>` that aren't valid XML tag names (must
    start with letter or underscore).  V1 firmware's tinyxml2 parser
    is lenient; Python stdlib's `xml.etree.ElementTree` isn't.  The
    Python parser preprocesses these tags to `<_3DAUDIO>` before
    parsing.
  - No `ALPHA0`, `ALPHASTALL`, or `KFIT` per flap — the V1 wizard
    didn't extract these.  The Python parser derives:
      - `alpha_0` from the `AOA_CURVE_FLAPS{i}` polynomial's constant
        term (4th element in the `x3,x2,x1,x0,type` 5-tuple).
      - `alpha_stall` as `stallwarn_aoa + 1.5°` (heuristic — sufficient
        for replay).
  - Vac's `alpha_0` for flap 0 reads as +4.62° because the box was
    mounted without programming the install-bias correction.  Not a
    parser bug; just an installation that wasn't bias-zeroed.

- `vac_decel_run.csv` — 30-second window from his SD log
  (`vac_log.csv` rows where timestamp ∈ [2860, 2890] seconds), at
  50 Hz.  Contains a clean decel-to-stall-break sequence:
    - 2865–2877s: AOA climbs 10° → 17° as IAS bleeds 65 → 47 kt
    - 2877.5s: stall break, roll snaps to -40°, vertical G → 0.28
    - 2877.5–2885s: pilot recovers, AOA un-stalls, IAS climbs back

  Column set is the older format used before [PR #353](https://github.com/flyonspeed/OnSpeed-Gen3/pull/353):
    - `AngleofAttack` (renamed to `DerivedAOA` post-#353)
    - No `efisPercentLift`, no per-flap percent-lift columns
    - Carries `boomAlpha`/`boomBeta` and full VN-300 (`vn*`) data

## Why these are checked in

These fixtures are the regression test for "the synth-record tool can
replay any OnSpeed customer's data."  If a future Python parser change
breaks V1 config or log handling, the tests against these files fail
loudly.  Without checked-in fixtures the V1 path bit-rots silently.

## Running the tests

From the repo root:
```bash
cd tools/synth-record
python3 -m pytest test_v1_formats.py -v
```

(Test file lives in `tools/synth-record/test_v1_formats.py` and
references this fixture directory.)
