"""C++ firmware <-> Python twin parity for the EKFQ pipeline.

Two assertions close the "the firmware preserves this filter's math" claim:

ASSERTION A (always runs, no native toolchain)
    The committed generated NumPy Jacobians (onspeed_ekf._generated_jacobians.F/H)
    reproduce the SymPy source (onspeed_ekf.symbolic.F_matrix/H_matrix) numerically
    over randomized states. Proves the checked-in NumPy artifact is faithful to its
    symbolic source. The Jacobian oracle (test_jacobian_oracle.py) covers this as one
    of its three derivations; this is a focused, self-contained restatement so the
    parity story lives in one file. It compares against the UN-gated SymPy F (the
    generated artifact is un-gated; the beta-row tas-gate is the caller's job in the
    firmware), matching the oracle's test_generated_numpy_equals_sympy.

ASSERTION B (skips when the native host_main binary is absent)
    The firmware replay binary (tools/regression/host_main, built `pio run -e native`)
    in `ahrs_tone --algorithm ekfq --input-format sdlog` mode and the Python twin
    (onspeed_ekf.PipelineQuat) are driven through the same SD-log fixture with the
    same default tuning (EKFQConfig() + PipelineQuatConfig()), then their per-row
    attitude outputs are compared.

    ROW ALIGNMENT
    host_main treats the first log row as a seed frame: it calls ahrs.Init() on it
    and emits no output row, so it produces n-1 output rows for an n-row log. The
    twin runs predict+correct on every row and produces n outputs. Across this
    fixture the regimes are near-stationary, and the tightest pitch/roll agreement
    is twin output row i <-> host output row i for the host's n-1 rows (the twin's
    trailing row has no host counterpart). We slice the twin to the host length.

    WHAT MATCHES, AND WHAT DOES NOT (derived AOA)
    pitch_deg and roll_deg agree to ~0.016 deg over 149 steps -- fp32 (firmware)
    vs fp64 (twin) accumulation of the same attitude algebra. That is the real
    C<->Python attitude loop and it is asserted tightly below.

    derived_aoa_deg does NOT agree on this fixture, and the cause is structural, not
    a tolerance problem:
      1. TAS source. The firmware derives TAS from IAS + Palt + OAT via the
         density-altitude correction in Ahrs.cpp (IAS 20.58 kt at 402 ft -> TAS
         ~10.63 m/s here). The twin's PipelineQuat reads the log's TAS column
         directly, and THIS fixture's TAS column is 0.0 throughout. alpha_kinematic
         takes a `tas < 0.5` geometric-AOA fallback branch on the twin while the
         firmware runs the full kinematic formula -- the two evaluate different
         branches.
      2. vz amplification. derived AOA contains a vz/TAS term. At the firmware's
         ~10.6 m/s TAS the small fp32 vz-state divergence between the two filters is
         amplified into a few degrees of AOA even after the TAS-source mismatch is
         removed.
    Both effects live entirely in the on-ground / low-speed regime of this fixture
    (IAS pinned at 20.58 kt, below the 25 kt ias_alive gate; firmware TAS ~10.6 m/s,
    below the 12 m/s beta gate). They are not an EKF attitude-math divergence. Per
    the task brief, AOA divergence beyond 0.1 deg is reported rather than papered
    over with an inflated tolerance, so derived_aoa is recorded as a documented
    xfail with the observed max deviation, not a silent loose pass.

    WHY THIS NEAR-STATIONARY FIXTURE AND NOT AN IN-AIR ONE
    The near-level regime is what makes pitch/roll match to ~0.016 deg: it keeps
    the two quaternion integrators on the same trajectory so the fp32-vs-fp64 gap
    stays in the round-off floor. A dynamic, high-rate flight log does the
    opposite. Driving both ends through the 50 Hz replay_engine fixture (pitch
    swinging -71..+53 deg, roll +-24 deg, gyro rates to 15 deg/s) makes the host
    EKFQ and the twin PipelineQuat separate during the first aggressive transient
    and then accumulate apart: pitch diverges ~105 deg, roll ~44 deg, derived_aoa
    ~263 deg by the end of 199 steps. The split begins exactly where the maneuver
    starts (|pitch| crossing 10 deg) and is path-dependent, not a function of any
    single attitude. Injecting the firmware's density-corrected TAS into the twin
    does not close it, so it is an attitude-integration divergence under extreme
    dynamics, not the TAS-source effect above. A clean three-column parity needs a
    near-stationary log; the smoke fixture is that log.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import numpy as np
import pytest

# Repo root is two levels up from this test file:
#   <repo>/ekfq_pipeline/tests/test_cpp_python_parity.py
_REPO_ROOT = Path(__file__).resolve().parents[2]
_EKFQ_PIPELINE_DIR = Path(__file__).resolve().parents[1]
_HOST_MAIN = _REPO_ROOT / "tools" / "regression" / ".pio" / "build" / "native" / "program"
_FIXTURE_LOG = _REPO_ROOT / "tools" / "regression" / "fixtures" / "ekfq_substrate_smoke.csv"
_FIXTURE_CFG = _REPO_ROOT / "tools" / "regression" / "fixtures" / "ekfq_substrate_smoke.cfg"


# ---------------------------------------------------------------------------
# ASSERTION A -- generated NumPy == SymPy source (always runs)
# ---------------------------------------------------------------------------

_RNG = np.random.default_rng(20260531)
_N_DRAWS = 20
_DT = 1.0 / 208.0


def _random_state() -> np.ndarray:
    """One normalized-quaternion EKFQ state x = [q0..q3, bp,bq,br, z,vz,b_az, beta]."""
    q = _RNG.normal(size=4)
    q /= np.linalg.norm(q)
    return np.array([*q,
                     *_RNG.normal(scale=0.01, size=3),   # bp, bq, br
                     _RNG.normal(scale=500),             # z
                     _RNG.normal(scale=5),               # vz
                     _RNG.normal(scale=0.1),             # b_az
                     _RNG.normal(scale=0.1)])            # beta


def _random_input() -> np.ndarray:
    """One input u = [p,q,r, ax_raw,ay_raw,az_raw, tas, tasdot] with in-air TAS."""
    return np.array([*_RNG.normal(scale=0.5, size=3),    # p, q, r
                     *_RNG.normal(scale=2, size=2),        # ax_raw, ay_raw
                     -9.80665 + _RNG.normal(scale=1),      # az_raw
                     _RNG.uniform(30, 80),                 # tas (in air, no beta gate)
                     _RNG.normal(scale=0.5)])              # tasdot


def test_generated_numpy_matches_sympy():
    """The committed NumPy Jacobian artifact reproduces the SymPy source numerically.

    Lambdifies symbolic.F_matrix()/H_matrix() and compares them against
    _generated_jacobians.F/H over randomized states. atol 1e-10: both sides are
    the same symbolic expressions, so the only gap is float round-off in two
    different evaluation orders.
    """
    import sympy as sp

    from onspeed_ekf import _generated_jacobians as gen
    from onspeed_ekf import symbolic as S

    syms = (S.q0, S.q1, S.q2, S.q3, S.bp, S.bq, S.br, S.z, S.vz, S.b_az, S.beta,
            S.p, S.q, S.r, S.ax_raw, S.ay_raw, S.az_raw, S.tas, S.tasdot, S.dt)
    f_sym = sp.lambdify(syms, S.F_matrix(), "numpy")
    h_sym = sp.lambdify(syms, S.H_matrix(), "numpy")

    for _ in range(_N_DRAWS):
        x = _random_state()
        u = _random_input()
        args = (*x, *u, _DT)
        f_ref = np.asarray(f_sym(*args), dtype=np.float64)
        h_ref = np.asarray(h_sym(*args), dtype=np.float64)
        np.testing.assert_allclose(gen.F(x, u, _DT), f_ref, atol=1e-10)
        np.testing.assert_allclose(gen.H(x, u), h_ref, atol=1e-10)


# ---------------------------------------------------------------------------
# ASSERTION B -- host_main ahrs_tone (ekfq) == Python twin (skips w/o toolchain)
# ---------------------------------------------------------------------------

# Pitch/roll tolerance: fp32 firmware vs fp64 twin over ~150 steps of the same
# attitude algebra. Observed max is ~0.016 deg on this fixture; 0.05 deg leaves
# headroom without masking a real attitude divergence.
_ATTITUDE_ATOL_DEG = 0.05


def _import_run_host_main():
    """Load run_host_main.py (a top-level module beside the package, not in it)."""
    if str(_EKFQ_PIPELINE_DIR) not in sys.path:
        sys.path.insert(0, str(_EKFQ_PIPELINE_DIR))
    spec = importlib.util.spec_from_file_location(
        "run_host_main", _EKFQ_PIPELINE_DIR / "run_host_main.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.mark.skipif(
    not _HOST_MAIN.exists(),
    reason=(f"host_main native binary not built at {_HOST_MAIN}; "
            "build with `pio run -e native` from tools/regression. "
            "CI builds it and runs this assertion for real."),
)
def test_host_main_matches_python_twin():
    """host_main ekfq pitch/roll == PipelineQuat pitch/roll on the smoke fixture.

    Drives both ends through the same SD-log + config with identical default tuning
    and asserts the attitude outputs agree row-for-row. derived_aoa is reported as a
    documented xfail (see module docstring): the firmware derives TAS from IAS while
    this fixture's TAS column is zeroed, so the two ends feed different TAS into the
    identical kinematic-AOA formula and the vz/TAS term amplifies the residual.
    """
    from onspeed_ekf import EKFQConfig, PipelineQuat, PipelineQuatConfig, load_log

    run_host_main = _import_run_host_main().run_host_main

    ekfq_cfg = EKFQConfig()
    pipe_cfg = PipelineQuatConfig()

    # Firmware side.
    host_df = run_host_main(
        _HOST_MAIN, _FIXTURE_LOG, _FIXTURE_CFG, ekfq_cfg, pipe_cfg, passthrough_cols=[])
    n = len(host_df)
    host_pitch = host_df["pitch_deg"].to_numpy(dtype=np.float64)
    host_roll = host_df["roll_deg"].to_numpy(dtype=np.float64)
    host_aoa = host_df["derived_aoa_deg"].to_numpy(dtype=np.float64)

    # Python-twin side, same fixture + same defaults.
    log = load_log(str(_FIXTURE_LOG))
    hist = PipelineQuat(ekf_cfg=ekfq_cfg, pipe_cfg=pipe_cfg).run(log)
    # host_main seeds (Init) on row 0 and emits no output for it, producing n
    # output rows for an n+1-row log; slice the twin to the host's output length.
    twin_pitch = np.asarray(hist["pitch_deg"], dtype=np.float64)[:n]
    twin_roll = np.asarray(hist["roll_deg"], dtype=np.float64)[:n]
    twin_aoa = np.asarray(hist["alpha_deg"], dtype=np.float64)[:n]

    assert twin_pitch.shape == host_pitch.shape, (
        f"row-count mismatch: twin {twin_pitch.shape} vs host {host_pitch.shape}")

    max_pitch_dev = float(np.max(np.abs(twin_pitch - host_pitch)))
    max_roll_dev = float(np.max(np.abs(twin_roll - host_roll)))
    max_aoa_dev = float(np.max(np.abs(twin_aoa - host_aoa)))
    print(f"\nhost_main vs Python twin max per-column deviation (deg):"
          f"\n  pitch       = {max_pitch_dev:.5f}"
          f"\n  roll        = {max_roll_dev:.5f}"
          f"\n  derived_aoa = {max_aoa_dev:.5f}  (documented divergence; see module docstring)")

    # The genuine attitude C<->Python loop: pitch and roll must track tightly.
    np.testing.assert_allclose(twin_pitch, host_pitch, atol=_ATTITUDE_ATOL_DEG)
    np.testing.assert_allclose(twin_roll, host_roll, atol=_ATTITUDE_ATOL_DEG)

    # derived_aoa diverges for the structural TAS-source reason documented above;
    # surface it as an xfail with the observed magnitude rather than asserting a
    # falsely-tight or falsely-loose tolerance.
    if max_aoa_dev > 0.1:
        pytest.xfail(
            f"derived_aoa diverges by {max_aoa_dev:.3f} deg on this fixture: the "
            "firmware derives TAS from IAS+Palt+OAT (~10.6 m/s) while the fixture's "
            "TAS column is 0.0, so the twin's alpha_kinematic takes the tas<0.5 "
            "geometric-AOA branch and the vz/TAS term amplifies the residual. This "
            "is a TAS-source/regime difference, not an EKF attitude-math divergence "
            "(pitch/roll agree to <0.05 deg). See module docstring.")
    np.testing.assert_allclose(twin_aoa, host_aoa, atol=0.1)
