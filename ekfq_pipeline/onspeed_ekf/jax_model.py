"""Independent JAX derivation of the EKFQ predict mean and measurement model.

This module is the *second independent derivation* used by the Jacobian oracle.
The oracle cross-checks three derivations of the filter's Jacobians — SymPy
symbolic, JAX autodiff (this file), and the hand-written code in
``ekf_quat.py`` — and asserts they agree. These functions exist only for that
cross-check; they are not used at runtime by the filter or the firmware.

Everything here is a pure function of state + inputs: no filter object, no
covariance, no internal state. Later oracle tasks autodiff these with
``jax.jacobian`` to obtain the predict Jacobian F and measurement Jacobian H.

State layout (matches ``ekf_quat.py`` exactly), indices 0..10:

    x = [q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta]

    q0..q3 : unit quaternion, NED body (q0 scalar)
    bp..br : roll/pitch/yaw gyro biases (rad/s)
    z      : pressure altitude (m, +up)
    vz     : vertical speed (m/s, +down)
    b_az   : earth-frame vertical accel bias (m/s²)
    beta   : sideslip angle (rad)

Predict input (7 elements):

    u = [p, q, r, ax, ay, az, tas]

Measurement input for ``h_accels`` (8 elements; adds tasdot):

    u = [p, q, r, ax, ay, az, tas, tasdot]
"""

from __future__ import annotations

import jax
import jax.numpy as jnp

jax.config.update("jax_enable_x64", True)

GRAVITY = 9.80665

# State indices.
Q0, Q1, Q2, Q3, BP, BQ, BR, Z, VZ, B_AZ, BETA = range(11)


def _common(x, u):
    """Shared scalar quantities for both predict-mean variants.

    Returns (a_D, beta_dot_gated_inputs) building blocks: the earth-down
    inertial acceleration and the pieces needed for the vertical and beta
    channels. tas-gating of beta is applied by the caller.
    """
    q0, q1, q2, q3 = x[Q0], x[Q1], x[Q2], x[Q3]
    bp, bq, br = x[BP], x[BQ], x[BR]
    b_az = x[B_AZ]

    p, q, r = u[0], u[1], u[2]
    ax, ay, az = u[3], u[4], u[5]

    # Bias-corrected gyros.
    p_c = p - bp
    q_c = q - bq
    r_c = r - br

    # Earth-Down inertial acceleration: R_be[2,:]·a_body + g − b_az.
    R20 = 2.0 * (q1 * q3 - q0 * q2)
    R21 = 2.0 * (q2 * q3 + q0 * q1)
    R22 = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3
    a_D = R20 * ax + R21 * ay + R22 * az + GRAVITY - b_az

    return p_c, q_c, r_c, a_D, ay


def _beta_dot(x, u, p_c, q_c, r_c, ay, tas, tas_min):
    """Sideslip rate, tas-gated. Uses jnp.where so grad traces both branches.

    The gravity-into-body-Y term is R_be[2,1] = 2(q2 q3 + q0 q1) — the same
    polynomial the firmware and symbolic model use. On the unit sphere this
    equals sin φ · cos θ, but the two forms have different off-manifold
    gradients, and jax.jacobian perturbs the raw quaternion freely. The
    polynomial form is what flies, so the derived F matches the firmware.
    """
    q0, q1, q2, q3 = x[Q0], x[Q1], x[Q2], x[Q3]
    R21 = 2.0 * (q2 * q3 + q0 * q1)
    beta_dot_active = (ay + R21 * GRAVITY) / tas - r_c
    return jnp.where(tas > tas_min, beta_dot_active, 0.0)


def _vertical_beta_update(new_x, x, u, dt, tas_min):
    """Apply the vertical channel (z, vz) and beta update; biases pass through."""
    p_c, q_c, r_c, a_D, ay = _common(x, u)
    tas = u[6]

    z = x[Z]
    vz = x[VZ]
    beta = x[BETA]

    z_new = z - (vz + 0.5 * a_D * dt) * dt
    vz_new = vz + a_D * dt

    beta_dot = _beta_dot(x, u, p_c, q_c, r_c, ay, tas, tas_min)
    beta_new = beta + dt * beta_dot

    new_x = new_x.at[Z].set(z_new)
    new_x = new_x.at[VZ].set(vz_new)
    new_x = new_x.at[BETA].set(beta_new)
    return new_x


def f_mean_linear(x, u, dt, tas_min):
    """Propagated state using the LINEAR quaternion step (q ← q + q̇·dt).

    Returns a length-11 jnp array. Biases (bp, bq, br) and b_az pass through
    unchanged.
    """
    q0, q1, q2, q3 = x[Q0], x[Q1], x[Q2], x[Q3]
    p_c, q_c, r_c, _a_D, _ay = _common(x, u)

    # Linear mean q̇ from q̇ = 0.5·W(ω)·q.
    q0d = 0.5 * (-q1 * p_c - q2 * q_c - q3 * r_c)
    q1d = 0.5 * (q0 * p_c + q2 * r_c - q3 * q_c)
    q2d = 0.5 * (q0 * q_c - q1 * r_c + q3 * p_c)
    q3d = 0.5 * (q0 * r_c + q1 * q_c - q2 * p_c)

    new_x = x
    new_x = new_x.at[Q0].set(q0 + q0d * dt)
    new_x = new_x.at[Q1].set(q1 + q1d * dt)
    new_x = new_x.at[Q2].set(q2 + q2d * dt)
    new_x = new_x.at[Q3].set(q3 + q3d * dt)

    new_x = _vertical_beta_update(new_x, x, u, dt, tas_min)
    return new_x


def f_mean_exact(x, u, dt, tas_min):
    """Propagated state using the EXACT exp-map quaternion step.

    q ← q ⊗ exp(½ω·dt). Returns a length-11 jnp array. Biases (bp, bq, br)
    and b_az pass through unchanged.
    """
    q0, q1, q2, q3 = x[Q0], x[Q1], x[Q2], x[Q3]
    p_c, q_c, r_c, _a_D, _ay = _common(x, u)

    # Rotation vector w = ω·dt; half_angle = 0.5·|w|.
    wx = p_c * dt
    wy = q_c * dt
    wz = r_c * dt
    ha = 0.5 * jnp.sqrt(wx * wx + wy * wy + wz * wz)

    dq_w = jnp.cos(ha)
    # ½·sinc(ha): guard the denominator so grad traces both branches safely.
    s_scale = jnp.where(ha > 1e-4,
                        0.5 * jnp.sin(ha) / jnp.where(ha > 1e-4, ha, 1.0),
                        0.5 * (1.0 - ha * ha / 6.0))
    dq_x = s_scale * wx
    dq_y = s_scale * wy
    dq_z = s_scale * wz

    # Hamilton product q_new = q ⊗ Δq.
    q0_new = q0 * dq_w - q1 * dq_x - q2 * dq_y - q3 * dq_z
    q1_new = q0 * dq_x + q1 * dq_w + q2 * dq_z - q3 * dq_y
    q2_new = q0 * dq_y - q1 * dq_z + q2 * dq_w + q3 * dq_x
    q3_new = q0 * dq_z + q1 * dq_y - q2 * dq_x + q3 * dq_w

    new_x = x
    new_x = new_x.at[Q0].set(q0_new)
    new_x = new_x.at[Q1].set(q1_new)
    new_x = new_x.at[Q2].set(q2_new)
    new_x = new_x.at[Q3].set(q3_new)

    new_x = _vertical_beta_update(new_x, x, u, dt, tas_min)
    return new_x


def h_accels(x, u):
    """Predicted body specific force (ax, ay, az) for the accel measurement.

    u is the 8-element form with tasdot at index 7:
        u = [p, q, r, ax, ay, az, tas, tasdot]

    Returns jnp.array([ax_pred, ay_pred, az_pred]).
    """
    q0, q1, q2, q3 = x[Q0], x[Q1], x[Q2], x[Q3]
    bq, br = x[BQ], x[BR]
    g = GRAVITY

    q = u[1]
    r = u[2]
    tas = u[6]
    tasdot = u[7]

    q_c = q - bq
    r_c = r - br

    ax_pred = -2.0 * g * (q1 * q3 - q0 * q2) + tasdot
    ay_pred = -2.0 * g * (q2 * q3 + q0 * q1) + tas * r_c
    az_pred = -g * (q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3) - tas * q_c

    return jnp.array([ax_pred, ay_pred, az_pred])
