"""Jacobian oracle: F and H derived three independent ways must agree.

  1. JAX autodiff of jax_model.f_mean_linear / h_accels
  2. SymPy symbolic.F_matrix() / H_matrix(), lambdified
  3. The hand-written F inside ekf_quat.py predict() (exposed as _F_last)

Three independent derivations agreeing is strong evidence the hand-written
firmware/twin Jacobians are correct. Also pins the predict consistency-by-design:
jacobian(exact mean) approx F to first order, differing at O((omega*dt)^3).

State layout everywhere: x = [q0,q1,q2,q3, bp,bq,br, z,vz,b_az, beta].

ON-GROUND BETA-ROW GATING
-------------------------
The beta channel is tas-gated: below tas_min the hand code (ekf_quat.py) and the
JAX model (jnp.where) zero the beta-row DYNAMICS partials (d(beta_dot)/dq0..q3 and
d(beta_dot)/dbr), while the SymPy F_matrix() is the un-gated derivative and always
carries the active beta dynamics. The identity self-term F[BETA, BETA] = 1 (beta
passes through unchanged) survives the gate in all three derivations. The on-ground
tests therefore zero SymPy F's gated beta-row columns before comparing (option b
from the task brief), and separately assert the JAX and hand gated columns are
actually zero on the ground. This tests both regimes from the single symbolic
source.

HAND-H IS NOT TESTED HERE (and that is intentional)
---------------------------------------------------
The hand-written H lives as a set of local arrays inside ekf_quat.py::correct() and
is not exposed; exposing it would require invasive surgery on correct() that the
task brief explicitly forbids. H correctness is instead pinned by the
JAX == SymPy == generated-NumPy three-way agreement below, plus the generated-C
header vs firmware fp32 check done in Task 3.
"""
import numpy as np
import jax
import jax.numpy as jnp
import sympy as sp
import pytest

from onspeed_ekf import jax_model as jm
from onspeed_ekf import symbolic as S
from onspeed_ekf import EKFQ
from onspeed_ekf import _generated_jacobians as gen

jax.config.update("jax_enable_x64", True)

DT = 1.0 / 208.0
TAS_MIN = 12.0
BETA = 10  # beta-row index in the 11-state layout
# The tas-gate controls only the beta-row DYNAMICS partials (d(beta_dot)/dq0..q3
# and d(beta_dot)/dbr). The identity self-term F[BETA, BETA] = 1 (beta passes
# through unchanged) is present in every regime, so the on-ground checks exclude
# it when asserting the gated entries are zero.
BETA_GATED_COLS = [0, 1, 2, 3, 6]  # q0, q1, q2, q3, br
RNG = np.random.default_rng(20260531)
N_DRAWS = 60

# lambdify SymPy F and H once. The symbol order here is the full
# (11 state, 8 input, dt) tuple consumed by _args() below.
_SYMS = (S.q0, S.q1, S.q2, S.q3, S.bp, S.bq, S.br, S.z, S.vz, S.b_az, S.beta,
         S.p, S.q, S.r, S.ax_raw, S.ay_raw, S.az_raw, S.tas, S.tasdot, S.dt)
_F_sym = sp.lambdify(_SYMS, S.F_matrix(), "numpy")
_H_sym = sp.lambdify(_SYMS, S.H_matrix(), "numpy")


def _random_state():
    q = RNG.normal(size=4)
    q /= np.linalg.norm(q)
    return np.array([*q,
                     *RNG.normal(scale=0.01, size=3),   # bp, bq, br
                     RNG.normal(scale=500),             # z
                     RNG.normal(scale=5),               # vz
                     RNG.normal(scale=0.1),             # b_az
                     RNG.normal(scale=0.1)])            # beta


def _random_input(in_air):
    # 8 elements: [p, q, r, ax_raw, ay_raw, az_raw, tas, tasdot].
    tas = RNG.uniform(30, 80) if in_air else RNG.uniform(0, 5)
    return np.array([*RNG.normal(scale=0.5, size=3),    # p, q, r
                     *RNG.normal(scale=2, size=2),       # ax_raw, ay_raw
                     -9.80665 + RNG.normal(scale=1),     # az_raw
                     tas,
                     RNG.normal(scale=0.5)])             # tasdot


def _args(x, u):
    """Flatten (state, 8-input) into the 20 scalar args the lambdified F/H take."""
    return (*x, u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], DT)


def _sympy_F(x, u, gate_beta):
    """Lambdified SymPy F. When gate_beta (on-ground), zero the beta-row dynamics
    partials to match the tas-gated hand/JAX F; the identity self-term
    F[BETA, BETA] = 1 is left in place since it survives the gate."""
    F = np.asarray(_F_sym(*_args(x, u)), dtype=np.float64)
    if gate_beta:
        F = F.copy()
        for col in BETA_GATED_COLS:
            F[BETA, col] = 0.0
    return F


def _sympy_H(x, u):
    return np.asarray(_H_sym(*_args(x, u)), dtype=np.float64)


# jax.jacobian of the predict mean / measurement model w.r.t. the 11-state.
_jac_f_linear = jax.jacobian(lambda s, u7: jm.f_mean_linear(s, u7, DT, TAS_MIN))
_jac_f_exact = jax.jacobian(lambda s, u7: jm.f_mean_exact(s, u7, DT, TAS_MIN))
_jac_h = jax.jacobian(lambda s, u8: jm.h_accels(s, u8))


def _jax_F(x, u):
    return np.asarray(_jac_f_linear(jnp.array(x), jnp.array(u[:7])))


def _jax_H(x, u):
    return np.asarray(_jac_h(jnp.array(x), jnp.array(u)))


@pytest.mark.parametrize("in_air", [True, False], ids=["in_air", "on_ground"])
def test_F_jax_equals_sympy(in_air):
    """JAX autodiff F == lambdified SymPy F over randomized states."""
    for _ in range(N_DRAWS):
        x = _random_state()
        u = _random_input(in_air)
        Fj = _jax_F(x, u)
        Fs = _sympy_F(x, u, gate_beta=not in_air)
        if not in_air:
            # The tas-gate must have zeroed JAX's beta-row dynamics on the ground.
            assert np.allclose(Fj[BETA, BETA_GATED_COLS], 0.0, atol=1e-12)
        np.testing.assert_allclose(Fj, Fs, atol=1e-9, rtol=1e-7)


@pytest.mark.parametrize("in_air", [True, False], ids=["in_air", "on_ground"])
def test_F_hand_equals_sympy(in_air):
    """Hand-written F (ekf_quat.predict -> _F_last) == lambdified SymPy F."""
    for _ in range(N_DRAWS):
        x = _random_state()
        u = _random_input(in_air)
        e = EKFQ()
        e.init()
        e.x = x.copy()
        # predict takes (p, q, r, ax_raw, ay_raw, az_raw, tas, dt).
        e.predict(u[0], u[1], u[2], u[3], u[4], u[5], u[6], DT)
        Fh = e._F_last
        Fs = _sympy_F(x, u, gate_beta=not in_air)
        if not in_air:
            assert np.allclose(Fh[BETA, BETA_GATED_COLS], 0.0, atol=1e-12)
        np.testing.assert_allclose(Fh, Fs, atol=1e-9, rtol=1e-7)


@pytest.mark.parametrize("in_air", [True, False], ids=["in_air", "on_ground"])
def test_H_jax_equals_sympy(in_air):
    """JAX autodiff H == lambdified SymPy H (no gating in H)."""
    for _ in range(N_DRAWS):
        x = _random_state()
        u = _random_input(in_air)
        Hj = _jax_H(x, u)
        Hs = _sympy_H(x, u)
        np.testing.assert_allclose(Hj, Hs, atol=1e-9, rtol=1e-7)


@pytest.mark.parametrize("in_air", [True, False], ids=["in_air", "on_ground"])
def test_generated_numpy_equals_sympy(in_air):
    """Generated NumPy artifact F/H == lambdified SymPy (same symbolic source)."""
    for _ in range(N_DRAWS):
        x = _random_state()
        u = _random_input(in_air)
        Fg = gen.F(x, u, DT)
        Hg = gen.H(x, u)
        # The generated artifact is the un-gated symbolic F, so compare against
        # the un-gated SymPy F for both regimes.
        Fs = _sympy_F(x, u, gate_beta=False)
        Hs = _sympy_H(x, u)
        np.testing.assert_allclose(Fg, Fs, atol=1e-10)
        np.testing.assert_allclose(Hg, Hs, atol=1e-10)


def test_exact_vs_linear_mean_consistency():
    """jacobian(exact mean) approx F to first order; the residual is the exact
    map's O((omega*dt)^3) term, ~3e-6 at 208 Hz. Threshold 1e-5."""
    # Representative in-air state with nonzero rates.
    x = np.array([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 500.0, 1.0, 0.0, 0.02])
    x[:4] /= np.linalg.norm(x[:4])
    u = np.array([0.4, -0.3, 0.5, 0.5, -0.2, -9.6, 55.0, 0.3])
    Fexact = np.asarray(_jac_f_exact(jnp.array(x), jnp.array(u[:7])))
    Flinear = _jax_F(x, u)
    resid = np.max(np.abs(Fexact - Flinear))
    assert resid < 1e-5, f"exact-vs-linear residual {resid:.3e} exceeds 1e-5"
    print(f"\nexact-vs-linear max|dF| residual at 208 Hz: {resid:.3e}")
