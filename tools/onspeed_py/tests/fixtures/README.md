# Test fixtures for `onspeed_py`

Three checked-in fixtures power the parser and replay tests:

## `vac_config.cfg` — V1 list-style config

Captured from a calibration flight (late 2025) before the config format
was modernized. Notable features:

- Top-level `<SETPOINT_*>` lists (comma-separated per-flap values),
  not `<FLAP_POSITION>` blocks.
- Tags like `<3DAUDIO>` that aren't valid XML tag names (must start
  with letter or underscore). The firmware's tinyxml2 parser is
  lenient; Python's stdlib parser isn't, so the loader preprocesses
  these tags to `<_3DAUDIO>` before parsing.
- No `ALPHA0`, `ALPHASTALL`, or `KFIT` per flap. The loader fills in
  defaults: `alpha_0 = 0.0`, `alpha_stall = stallwarn + 1.5`,
  `k_fit = 0.0`. (Replay-quality approximations; precise values need
  re-fitting against the source log.)

Three detents at 0 / 20 / 40 deg, pot values 129 / 158 / 206.

## `vac_decel_run.csv` — V1 SD-log slice

A 30-second window from the same calibration flight (timestamps in
`[2860, 2890]` seconds), at ~50 Hz. Contains a clean
decel-to-stall-break sequence:

- 2865–2877s: AOA climbs 10° → 17° as IAS bleeds 65 → 47 kt.
- 2877.5s: stall break, roll snaps to -40°, vertical G → 0.28.
- 2877.5–2885s: pilot recovers, AOA un-stalls, IAS climbs back.

Column set is the older format (pre-PR #353): `AngleofAttack` (not
`DerivedAOA`), no `efisPercentLift`, no per-flap percent-lift
columns, plus full VN-300 (`vn*`) data.

## `n720ak_config.cfg` — new per-FLAP_POSITION-block config

Current-firmware config from N720AK (RV-10). Three detents at
0 / 16 / 33 deg, with calibrated `ALPHA0`, `ALPHASTALL`, and `KFIT`
populated per flap. Used by `test_config_new.py` to verify the new
parser path picks up the values that `test_config_v1.py` defaults.

## Why these are checked in

These are the regression test for "the Python tooling can replay any
OnSpeed customer's data." Without checked-in fixtures, the V1 path
would bit-rot silently when someone changes the parser.
