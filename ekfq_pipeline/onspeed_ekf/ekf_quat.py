"""11-state quaternion EKF: attitude + sideslip + VSI + altitude.

State vector:
    x = [q0, q1, q2, q3, bp, bq, br, z, vz, b_az, beta]

    q0..q3 : unit quaternion, NED body (q0 scalar). Encodes attitude.
    bp..br : roll/pitch/yaw gyro biases (rad/s).
    z      : pressure altitude (m, +up; matches CSV Palt).
    vz     : vertical speed (m/s, +down; matches vnVelNedDown).
    b_az   : earth-frame vertical accel bias (m/s²).
    beta   : sideslip angle (rad). Positive = relative wind from the right
             (left rudder needed for coordinated flight).

Alpha (AOA) is NOT a state. It is computed as a *derived output* from the
state at every step using the universal kinematic formula:

    alpha = atan2(sin θ, cos φ cos θ)
          + asin( (vz/TAS − sin φ cos θ sin β) /
                  (cos β · √(sin² θ + cos² φ cos² θ)) )

This is geometrically exact for coordinated flight (and degrades gracefully
in slips because we include the β correction term). No aircraft constants.

Sideslip dynamics — also universal physics, no aircraft constants:

    β̇ = (f_body_y + sin φ cos θ g) / TAS  −  r·cos α  +  p·sin α

where f_body_y is the raw (uncompensated) lateral specific force from the
accelerometer in m/s², and α is the kinematic alpha computed inline.

Sideslip observability comes from two sources:
1. A weak β = 0 prior measurement (β tends toward zero over long timescales
   because most flight is coordinated). Strength tunable via R_beta_prior.
2. Adaptive R_ay: the lateral-accel measurement noise grows with β² so the
   filter de-weights ay during sustained slips (it can't trust gravity-only
   lateral G when the aircraft is uncoordinated). Strength tunable via
   k_beta_R.

Sign conventions match the firmware EKF6 at the input boundary: body
specific force in standard NED body (az ≈ -g level), gyros in aerospace
sense (+p drives right-wing-down, +q drives nose-up). The Pipeline adapter
does the sign flip from firmware's +1g-level convention.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


GRAVITY_MPS2 = 9.80665


# ---------------------------------------------------------------------------
# Config / state value objects
# ---------------------------------------------------------------------------


@dataclass
class EKFQConfig:
    # Process noise (continuous spectral density)
    q_quat:     float = 0.5e-3      # per quaternion component
    q_bias:     float = 2.08e-3     # gyro biases
    q_z:        float = 1e-3
    q_vz:       float = 1.0
    q_b_az:     float = 1e-5
    q_beta:     float = 5e-4        # sideslip random walk; lower = slower drift

    # Measurement noise (discrete variance)
    r_ax:    float = 0.5
    r_ay:    float = 0.5
    r_az:    float = 0.5
    r_baro:  float = 0.79078
    r_beta_prior: float = 0.05      # β=0 weak prior (variance in rad²);
                                    # smaller value = stronger pull toward zero

    # Gyro-bias = 0 weak priors. Without these the bias states drift
    # unboundedly during aerobatic excursions (the filter uses them to
    # absorb model mismatch). Variance is (0.05 rad/s)² ≈ 2.9°/s — wide
    # enough to allow real bias estimates, tight enough to bound drift.
    r_bias_prior: float = 7.6e-4    # (0.05 rad/s)²

    # β-adaptive R_ay scaling: R_ay_eff = R_ay · (1 + k_beta_R · β²)
    # Default chosen so a 5° slip inflates R_ay by ~2x — modest, not punishing.
    k_beta_R: float = 5.0

    # Initial covariance
    p_quat:     float = 0.05
    p_bias:     float = 0.01
    p_z:        float = 100.0
    p_vz:       float = 4.0
    p_b_az:     float = 1.0
    p_beta:     float = 0.01        # rad²

    # Minimum TAS (m/s) below which β dynamics are damped — below stall,
    # the formula degrades and would otherwise inject noise.
    tas_min_mps: float = 12.0       # ≈ 24 kt


@dataclass
class EKFQState:
    q0: float = 1.0
    q1: float = 0.0
    q2: float = 0.0
    q3: float = 0.0
    bp: float = 0.0
    bq: float = 0.0
    br: float = 0.0
    z: float  = 0.0
    vz: float = 0.0
    b_az: float = 0.0
    beta: float = 0.0

    @property
    def pitch_rad(self) -> float:
        s = 2.0 * (self.q0 * self.q2 - self.q3 * self.q1)
        if s > 1.0:  s = 1.0
        if s < -1.0: s = -1.0
        return float(np.arcsin(s))

    @property
    def roll_rad(self) -> float:
        return float(np.arctan2(2.0 * (self.q0 * self.q1 + self.q2 * self.q3),
                                1.0 - 2.0 * (self.q1 ** 2 + self.q2 ** 2)))

    @property
    def yaw_rad(self) -> float:
        return float(np.arctan2(2.0 * (self.q0 * self.q3 + self.q1 * self.q2),
                                1.0 - 2.0 * (self.q2 ** 2 + self.q3 ** 2)))

    @property
    def pitch_deg(self) -> float: return float(np.rad2deg(self.pitch_rad))
    @property
    def roll_deg(self)  -> float: return float(np.rad2deg(self.roll_rad))
    @property
    def yaw_deg(self)   -> float: return float(np.rad2deg(self.yaw_rad))
    @property
    def beta_deg(self)  -> float: return float(np.rad2deg(self.beta))


# Compute kinematic AOA from state quantities (universal formula).
def alpha_kinematic(phi: float, theta: float, vz: float, tas: float,
                    beta: float) -> float:
    """Exact universal kinematic AOA for coordinated symmetric flight,
    with sideslip correction. Returns alpha in radians.

    α = atan2(sin θ, cos φ cos θ)
      + asin( (vz/TAS − sin φ cos θ sin β) / (cos β · √(sin²θ + cos²φ cos²θ)) )

    Falls back to atan(tan θ / cos φ) at TAS=0 to avoid blowup near stall.
    """
    if tas < 0.5:
        # No reliable air-relative velocity — return level-flight AOA.
        return float(np.arctan2(np.sin(theta), max(1e-6, np.cos(phi) * np.cos(theta))))
    sth = np.sin(theta); cth = np.cos(theta)
    sph = np.sin(phi);   cph = np.cos(phi)
    sb  = np.sin(beta);  cb  = np.cos(beta)
    base = np.arctan2(sth, cph * cth)
    denom = max(1e-6, cb * np.sqrt(sth * sth + cph * cph * cth * cth))
    num = (vz / tas) - sph * cth * sb
    arg = num / denom
    if arg > 1.0:  arg = 1.0
    if arg < -1.0: arg = -1.0
    return float(base + np.arcsin(arg))


# ---------------------------------------------------------------------------
# Main filter
# ---------------------------------------------------------------------------


N_STATES = 11
Q0, Q1, Q2, Q3, BP, BQ, BR, Z, VZ, B_AZ, BETA = range(N_STATES)


def _euler_to_quat(roll: float, pitch: float, yaw: float = 0.0) -> np.ndarray:
    cy = np.cos(yaw * 0.5)
    sy = np.sin(yaw * 0.5)
    cp = np.cos(pitch * 0.5)
    sp = np.sin(pitch * 0.5)
    cr = np.cos(roll * 0.5)
    sr = np.sin(roll * 0.5)
    return np.array([
        cr * cp * cy + sr * sp * sy,
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
    ])


class EKFQ:
    """11-state quaternion EKF with attitude + VSI/altitude + sideslip."""

    def __init__(self, cfg: EKFQConfig | None = None) -> None:
        self.cfg = cfg if cfg is not None else EKFQConfig()
        self.x = np.zeros(N_STATES, dtype=np.float64)
        self.x[Q0] = 1.0
        self.P = np.eye(N_STATES, dtype=np.float64)
        self._initialized = False

    # ----- init / reset --------------------------------------------------

    def init(self, phi0: float = 0.0, theta0: float = 0.0, z0: float = 0.0) -> None:
        c = self.cfg
        q = _euler_to_quat(phi0, theta0, 0.0)
        self.x[:] = 0.0
        self.x[Q0:Q3 + 1] = q
        self.x[Z] = z0
        self.P[:] = 0.0
        for i in (Q0, Q1, Q2, Q3):
            self.P[i, i] = c.p_quat
        for i in (BP, BQ, BR):
            self.P[i, i] = c.p_bias
        self.P[Z,    Z]    = c.p_z
        self.P[VZ,   VZ]   = c.p_vz
        self.P[B_AZ, B_AZ] = c.p_b_az
        self.P[BETA, BETA] = c.p_beta
        self._initialized = True

    def reset_vertical_covariance(self, baro_z: float | None = None) -> None:
        if not self._initialized:
            return
        for idx in (Z, VZ, B_AZ):
            self.P[idx, :] = 0.0
            self.P[:, idx] = 0.0
        self.P[Z,    Z]    = self.cfg.p_z
        self.P[VZ,   VZ]   = self.cfg.p_vz
        self.P[B_AZ, B_AZ] = self.cfg.p_b_az
        if baro_z is not None:
            self.x[Z] = baro_z

    # ----- accessors -----------------------------------------------------

    @property
    def state(self) -> EKFQState:
        x = self.x
        return EKFQState(q0=float(x[Q0]), q1=float(x[Q1]), q2=float(x[Q2]), q3=float(x[Q3]),
                         bp=float(x[BP]), bq=float(x[BQ]), br=float(x[BR]),
                         z=float(x[Z]), vz=float(x[VZ]), b_az=float(x[B_AZ]),
                         beta=float(x[BETA]))

    def alpha_kinematic_rad(self, tas: float) -> float:
        """Compute kinematic AOA from current state and given TAS."""
        s = self.state
        return alpha_kinematic(s.roll_rad, s.pitch_rad, s.vz, tas, s.beta)

    def _renormalize(self) -> None:
        q = self.x[Q0:Q3 + 1]
        n = np.linalg.norm(q)
        if n > 1e-9:
            self.x[Q0:Q3 + 1] = q / n

    # ----- predict -------------------------------------------------------

    def predict(self, p: float, q: float, r: float,
                ax_raw: float, ay_raw: float, az_raw: float,
                tas: float, dt: float) -> None:
        """Propagate state and covariance one step.

        p, q, r        : body gyro rates (rad/s, aerospace convention).
        ax_raw, ay_raw,
        az_raw         : body specific force (m/s², standard NED-body,
                         az = -g level). RAW smoothed values — NOT
                         centripetal-compensated.
        tas            : true airspeed (m/s, pipeline input).
        dt             : seconds since last call.
        """
        if not self._initialized:
            self.init()

        q0, q1, q2, q3 = self.x[Q0], self.x[Q1], self.x[Q2], self.x[Q3]
        bp, bq, br     = self.x[BP], self.x[BQ], self.x[BR]
        vz, b_az, beta = self.x[VZ], self.x[B_AZ], self.x[BETA]

        # Bias-corrected gyros
        p_c, q_c, r_c = p - bp, q - bq, r - br

        # Quaternion derivative: q_dot = 0.5 · Ω(ω) · q
        q0_dot = 0.5 * (-q1 * p_c - q2 * q_c - q3 * r_c)
        q1_dot = 0.5 * ( q0 * p_c + q2 * r_c - q3 * q_c)
        q2_dot = 0.5 * ( q0 * q_c - q1 * r_c + q3 * p_c)
        q3_dot = 0.5 * ( q0 * r_c + q1 * q_c - q2 * p_c)

        # Earth-Down inertial acceleration (drives vz). R_be[2,:]·a_body + g − b_az.
        R20 = 2.0 * (q1 * q3 - q0 * q2)
        R21 = 2.0 * (q2 * q3 + q0 * q1)
        R22 = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3
        a_D = R20 * ax_raw + R21 * ay_raw + R22 * az_raw + GRAVITY_MPS2 - b_az

        # Attitude angles (needed for beta dynamics and Jacobians)
        sin_th = 2.0 * (q0 * q2 - q3 * q1)
        sin_th_clamp = max(-1.0, min(1.0, sin_th))
        cos_th_sq = max(0.0, 1.0 - sin_th_clamp * sin_th_clamp)
        cos_th = float(np.sqrt(cos_th_sq))
        if cos_th < 1e-6:
            cos_th = 1e-6   # near gimbal; clamp to avoid division
        # roll: sin(φ) = 2(q2 q3 + q0 q1) / cos(θ);  cos(φ) = (q0² - q1² + q2² - q3²) / cos(θ)
        # (Equivalent to standard quaternion→Euler formulas, rescaled by cos θ.)
        # Safer numerically: compute atan2 directly.
        roll = np.arctan2(2.0 * (q0 * q1 + q2 * q3),
                          1.0 - 2.0 * (q1 * q1 + q2 * q2))
        sin_ph = np.sin(roll)
        cos_ph = np.cos(roll)

        # β̇ ≈ (f_body_y + sin φ · cos θ · g) / TAS − r
        #
        # Full formula has − r·cos α + p·sin α; we drop the α dependence
        # (cos α ≈ 1, sin α ≈ 0) because the α-coupling created a positive
        # feedback path with the β-adaptive R_ay (β large → trust ay less
        # → roll uncorrected → ay grows → α via vz/TAS grows → β feedback).
        # The error introduced is O(α·rate) which Q_β absorbs.
        if tas > self.cfg.tas_min_mps:
            beta_dot = ((ay_raw + sin_ph * cos_th * GRAVITY_MPS2) / tas
                        - r_c)
        else:
            beta_dot = 0.0

        # ---- Integrate state ----
        new_x = self.x.copy()
        new_x[Q0] = q0 + dt * q0_dot
        new_x[Q1] = q1 + dt * q1_dot
        new_x[Q2] = q2 + dt * q2_dot
        new_x[Q3] = q3 + dt * q3_dot
        # bp, bq, br unchanged
        new_x[Z]    = self.x[Z] - (vz + 0.5 * a_D * dt) * dt
        new_x[VZ]   = vz + a_D * dt
        # b_az unchanged
        new_x[BETA] = beta + dt * beta_dot

        # ---- F = I + dt · df/dx ----
        F = np.eye(N_STATES)

        # Quaternion-block partials (from q̇ = 0.5·W(ω)·q):
        F[Q0, Q1] = -0.5 * dt * p_c
        F[Q0, Q2] = -0.5 * dt * q_c
        F[Q0, Q3] = -0.5 * dt * r_c
        F[Q1, Q0] =  0.5 * dt * p_c
        F[Q1, Q2] =  0.5 * dt * r_c
        F[Q1, Q3] = -0.5 * dt * q_c
        F[Q2, Q0] =  0.5 * dt * q_c
        F[Q2, Q1] = -0.5 * dt * r_c
        F[Q2, Q3] =  0.5 * dt * p_c
        F[Q3, Q0] =  0.5 * dt * r_c
        F[Q3, Q1] =  0.5 * dt * q_c
        F[Q3, Q2] = -0.5 * dt * p_c

        # ∂q̇/∂bias = −∂q̇/∂ω
        F[Q0, BP] =  0.5 * dt * q1
        F[Q0, BQ] =  0.5 * dt * q2
        F[Q0, BR] =  0.5 * dt * q3
        F[Q1, BP] = -0.5 * dt * q0
        F[Q1, BQ] =  0.5 * dt * q3
        F[Q1, BR] = -0.5 * dt * q2
        F[Q2, BP] = -0.5 * dt * q3
        F[Q2, BQ] = -0.5 * dt * q0
        F[Q2, BR] =  0.5 * dt * q1
        F[Q3, BP] =  0.5 * dt * q2
        F[Q3, BQ] = -0.5 * dt * q1
        F[Q3, BR] = -0.5 * dt * q0

        # Partials of a_D w.r.t. quaternion (for vertical channel).
        dD_dq0 = -2.0 * q2 * ax_raw + 2.0 * q1 * ay_raw + 2.0 * q0 * az_raw
        dD_dq1 =  2.0 * q3 * ax_raw + 2.0 * q0 * ay_raw - 2.0 * q1 * az_raw
        dD_dq2 = -2.0 * q0 * ax_raw + 2.0 * q3 * ay_raw - 2.0 * q2 * az_raw
        dD_dq3 =  2.0 * q1 * ax_raw + 2.0 * q2 * ay_raw + 2.0 * q3 * az_raw
        dt2 = dt * dt

        F[VZ, Q0]   = dt * dD_dq0
        F[VZ, Q1]   = dt * dD_dq1
        F[VZ, Q2]   = dt * dD_dq2
        F[VZ, Q3]   = dt * dD_dq3
        F[VZ, B_AZ] = -dt

        F[Z, Q0]   = -0.5 * dt2 * dD_dq0
        F[Z, Q1]   = -0.5 * dt2 * dD_dq1
        F[Z, Q2]   = -0.5 * dt2 * dD_dq2
        F[Z, Q3]   = -0.5 * dt2 * dD_dq3
        F[Z, VZ]   = -dt
        F[Z, B_AZ] = +0.5 * dt2

        # β row Jacobian (corrected from earlier version):
        #
        # β̇ = (ay_raw + g_body_y) / TAS  −  r_c
        # With r_c = r − br, expand to: β̇ = ... − r + br
        #
        # → ∂β̇/∂br = +1, so F[BETA, BR] = +dt  (previously had wrong sign)
        # → ∂β̇/∂q_i = (g/TAS) · ∂R_be[2,1]/∂q_i, where
        #     R_be[2,1] = 2(q2 q3 + q0 q1)
        #   so the partials are 2·{q1, q0, q3, q2} respectively.
        if tas > self.cfg.tas_min_mps:
            F[BETA, BR] = +dt
            g_over_tas = GRAVITY_MPS2 / tas
            F[BETA, Q0] = dt * g_over_tas * 2.0 * q1
            F[BETA, Q1] = dt * g_over_tas * 2.0 * q0
            F[BETA, Q2] = dt * g_over_tas * 2.0 * q3
            F[BETA, Q3] = dt * g_over_tas * 2.0 * q2

        # ---- Covariance update ----
        P_new = F @ self.P @ F.T

        c = self.cfg
        Qdiag = np.array([
            c.q_quat, c.q_quat, c.q_quat, c.q_quat,
            c.q_bias, c.q_bias, c.q_bias,
            c.q_z, c.q_vz, c.q_b_az,
            c.q_beta,
        ])
        P_new[np.diag_indices(N_STATES)] += Qdiag * dt

        self.x = new_x
        self.P = P_new
        self._renormalize()

    # ----- correct -------------------------------------------------------

    def correct(self, ax_meas: float, ay_meas: float, az_meas: float,
                baro_z: float,
                *,
                # Inputs needed for centripetal/TASdot modelled in h(x).
                # These are the raw gyro readings (m/s, rad/s) and TAS
                # rate — NOT EKF states. Passing them lets h(x) include
                # the non-gravity contributions to body specific force.
                tas_mps: float = 0.0,
                pitch_rate_rps: float = 0.0,
                yaw_rate_rps:   float = 0.0,
                tasdot_mps2:    float = 0.0,
                update_baro: bool = True,
                update_beta_prior: bool = True,
                update_bias_prior: bool = True) -> None:
        """Apply accel + baro + β=0 prior + gyro-bias=0 prior measurements.

        Accelerometer h(x) now models centripetal and TASdot contributions
        explicitly:

          h_ax = g·sin(θ) + TASdot
          h_ay = −g·cos(θ)·sin(φ) + TAS·(r − br)
          h_az = −g·cos(θ)·cos(φ) − TAS·(q − bq)

        So pass `ax_meas/ay_meas/az_meas` as RAW smoothed body specific force
        (install-bias-rotated + EMA smoothed, but with no centripetal /
        TASdot subtraction). The state's bias-corrected gyros (q − bq,
        r − br) appear inside h(x); the new H entries ∂h_ay/∂br = −TAS and
        ∂h_az/∂bq = +TAS couple lateral/vertical accel residuals to gyro
        bias states, giving the filter direct observability of bq/br
        during banked turns and pull-ups. Strict improvement over the
        firmware's upstream centripetal subtraction, which used the
        previous-step bias estimate and missed this coupling.
        """
        if not self._initialized:
            self.init()

        q0, q1, q2, q3 = self.x[Q0], self.x[Q1], self.x[Q2], self.x[Q3]
        bp, bq, br = self.x[BP], self.x[BQ], self.x[BR]
        z, beta = self.x[Z], self.x[BETA]
        g = GRAVITY_MPS2

        # Bias-corrected gyro rates used inside h(x) for centripetal terms.
        q_c = pitch_rate_rps - bq
        r_c = yaw_rate_rps - br

        # Predicted body specific force = gravity-only + non-gravity
        # contributions (TASdot in body X, centripetal in body Y/Z).
        # Forward TASdot: directly adds to ax (no state coupling beyond θ).
        # Lateral centripetal: TAS·r in body Y (positive in right turn).
        # Vertical centripetal: −TAS·q in body Z (more-negative az under pull-up).
        ax_pred = -2.0 * g * (q1 * q3 - q0 * q2) + tasdot_mps2
        ay_pred = -2.0 * g * (q2 * q3 + q0 * q1) + tas_mps * r_c
        az_pred =       -g * (q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3) - tas_mps * q_c

        z_meas = [ax_meas, ay_meas, az_meas]
        h      = [ax_pred, ay_pred, az_pred]

        # R_ay adaptive: inflate by (1 + k_beta_R · β²) — distrust lateral
        # accel when uncoordinated.
        r_ay_eff = self.cfg.r_ay * (1.0 + self.cfg.k_beta_R * beta * beta)
        R_diag = [self.cfg.r_ax, r_ay_eff, self.cfg.r_az]

        H_rows: list[np.ndarray] = []
        row_ax = np.zeros(N_STATES)
        row_ax[Q0] =  2.0 * g * q2
        row_ax[Q1] = -2.0 * g * q3
        row_ax[Q2] =  2.0 * g * q0
        row_ax[Q3] = -2.0 * g * q1
        H_rows.append(row_ax)

        row_ay = np.zeros(N_STATES)
        row_ay[Q0] = -2.0 * g * q1
        row_ay[Q1] = -2.0 * g * q0
        row_ay[Q2] = -2.0 * g * q3
        row_ay[Q3] = -2.0 * g * q2
        # ∂h_ay/∂br = ∂(TAS·(r-br))/∂br = −TAS.  This is the new
        # state-coupling that lets banked-turn lateral-G residuals
        # constrain the yaw-rate bias state directly.
        row_ay[BR] = -tas_mps
        H_rows.append(row_ay)

        row_az = np.zeros(N_STATES)
        row_az[Q0] = -2.0 * g * q0
        row_az[Q1] =  2.0 * g * q1
        row_az[Q2] =  2.0 * g * q2
        row_az[Q3] = -2.0 * g * q3
        # ∂h_az/∂bq = ∂(−TAS·(q-bq))/∂bq = +TAS.  Pull-up vertical-G
        # residuals now directly constrain the pitch-rate bias state.
        row_az[BQ] = +tas_mps
        H_rows.append(row_az)

        if update_baro:
            z_meas.append(baro_z)
            h.append(z)
            row_baro = np.zeros(N_STATES); row_baro[Z] = 1.0
            H_rows.append(row_baro)
            R_diag.append(self.cfg.r_baro)

        if update_beta_prior:
            z_meas.append(0.0)  # β=0 prior
            h.append(beta)
            row_beta = np.zeros(N_STATES); row_beta[BETA] = 1.0
            H_rows.append(row_beta)
            R_diag.append(self.cfg.r_beta_prior)

        if update_bias_prior:
            for idx in (BP, BQ, BR):
                z_meas.append(0.0)
                h.append(self.x[idx])
                row = np.zeros(N_STATES); row[idx] = 1.0
                H_rows.append(row)
                R_diag.append(self.cfg.r_bias_prior)

        z_arr = np.asarray(z_meas, dtype=np.float64)
        h_arr = np.asarray(h, dtype=np.float64)
        H = np.vstack(H_rows)
        R = np.diag(R_diag)

        y = z_arr - h_arr
        PHt = self.P @ H.T
        S = H @ PHt + R
        try:
            K = np.linalg.solve(S.T, PHt.T).T
        except np.linalg.LinAlgError:
            return

        self.x = self.x + K @ y
        I_KH = np.eye(N_STATES) - K @ H
        self.P = I_KH @ self.P @ I_KH.T + K @ R @ K.T

        self._renormalize()
