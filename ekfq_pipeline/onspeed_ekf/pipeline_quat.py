"""Pipeline using the 11-state quaternion+sideslip EKF.

Replays a flight log through the same firmware-style signal chain as before
(installation bias 3D rotation, EMA, centripetal/TASdot comp, iasAlive
gating), but with the new filter that has β instead of α as the 11th state.
Alpha is exposed as a derived output via the universal kinematic formula.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .data import FlightLog
from .ekf_quat import EKFQ, EKFQConfig, GRAVITY_MPS2, alpha_kinematic


_KT_TO_MPS = 0.514444
_FT_TO_M = 0.3048


@dataclass
class PipelineQuatConfig:
    # Installation biases come from the per-aircraft calibration in
    # onspeed*.cfg. They are NOT free params for the EKF tuner — they
    # are aircraft-specific physical constants the OnSpeed wizard measures
    # once per install. Defaults below match the testbed RV-4 from the
    # `onspeed2.cfg` saved with the flight log.
    pitch_bias_deg: float = -5.7027645
    roll_bias_deg:  float =  0.061873168
    # The GX/GY/GZ entries in onspeed*.cfg are NOT static gyro bias values
    # despite the field names suggesting otherwise. Verified empirically
    # against the v11 ESKF: on truly stationary ground (gyro_mag < 0.5
    # dps), raw gyros average ~0.005-0.016 dps. Subtracting the cfg values
    # (-0.48 / +0.31 / +0.22) creates an artificial 0.3-0.5 dps bias that
    # the EKF can't refine in flight (centripetal coupling vanishes on
    # ground, and r_bias_prior pins bp/bq/br near 0 in cruise). Result was
    # a cruise pitch RMS of 2.04° (with the wrong subtraction) vs 1.43°
    # (with zero subtraction). The firmware never applied these values
    # either — leaving them at 0 matches both firmware behaviour and the
    # measured ground reality.
    gx_bias_dps: float = 0.0
    gy_bias_dps: float = 0.0
    gz_bias_dps: float = 0.0
    # Static pressure bias (mb). Subtracted from raw PStatic before
    # altitude conversion. Currently we feed the firmware's pre-computed
    # Palt column which already has this applied; kept here as a knob
    # for future raw-pressure pipelines.
    pstatic_bias_mb: float = 0.56274414
    # Tunable signal-chain parameters (these CAN be Optuna params).
    accel_ema_alpha: float = 0.060899
    comp_fade_tau_sec: float = 0.5
    ias_alive_kt: float = 25.0
    tasdot_ema_alpha: float = 0.05


class PipelineQuat:
    """Quaternion+sideslip EKF replay over a flight log."""

    def __init__(
        self,
        ekf_cfg: EKFQConfig | None = None,
        pipe_cfg: PipelineQuatConfig | None = None,
    ) -> None:
        self.ekf_cfg = ekf_cfg if ekf_cfg is not None else EKFQConfig()
        self.pipe_cfg = pipe_cfg if pipe_cfg is not None else PipelineQuatConfig()
        self.ekf = EKFQ(self.ekf_cfg)
        self.history: dict[str, np.ndarray] = {}

    def _seed_attitude(self, log: FlightLog) -> tuple[float, float]:
        ax = float(log.df["ForwardG"].iloc[0])
        ay = float(log.df["LateralG"].iloc[0])
        az = float(log.df["VerticalG"].iloc[0])
        pitch_deg = np.rad2deg(np.arctan2(ax, np.sqrt(ay * ay + az * az)))
        roll_deg  = -np.rad2deg(np.arctan2(ay, np.sqrt(ax * ax + az * az)))
        pitch_deg += self.pipe_cfg.pitch_bias_deg
        roll_deg  += self.pipe_cfg.roll_bias_deg
        return float(np.deg2rad(roll_deg)), float(np.deg2rad(pitch_deg))

    def run(self, log: FlightLog) -> dict[str, np.ndarray]:
        n = log.n
        df = log.df
        cfg = self.pipe_cfg

        accel_fwd_g  = df["ForwardG"].to_numpy(dtype=np.float64)
        accel_lat_g  = df["LateralG"].to_numpy(dtype=np.float64)
        accel_vert_g = df["VerticalG"].to_numpy(dtype=np.float64)
        roll_rate_dps  = df["RollRate"].to_numpy(dtype=np.float64)
        pitch_rate_dps = df["PitchRate"].to_numpy(dtype=np.float64)
        yaw_rate_dps   = df["YawRate"].to_numpy(dtype=np.float64)
        tas_kt  = df["TAS"].to_numpy(dtype=np.float64)
        ias_kt  = df["IAS"].to_numpy(dtype=np.float64)
        palt_ft = df["Palt"].to_numpy(dtype=np.float64)
        dt_arr = log.dt

        phi0, theta0 = self._seed_attitude(log)
        z0 = float(palt_ft[0]) * _FT_TO_M
        self.ekf.init(phi0=phi0, theta0=theta0, z0=z0)

        sp = np.sin(np.deg2rad(cfg.pitch_bias_deg))
        cp = np.cos(np.deg2rad(cfg.pitch_bias_deg))
        sr = np.sin(np.deg2rad(cfg.roll_bias_deg))
        cr = np.cos(np.deg2rad(cfg.roll_bias_deg))

        pitch_deg = np.empty(n, dtype=np.float32)
        roll_deg  = np.empty(n, dtype=np.float32)
        yaw_deg   = np.empty(n, dtype=np.float32)
        alpha_deg = np.empty(n, dtype=np.float32)    # derived kinematic AOA
        beta_deg  = np.empty(n, dtype=np.float32)
        vz_mps    = np.empty(n, dtype=np.float32)
        z_m       = np.empty(n, dtype=np.float32)
        bp_dps    = np.empty(n, dtype=np.float32)
        bq_dps    = np.empty(n, dtype=np.float32)
        br_dps    = np.empty(n, dtype=np.float32)
        b_az_mps2 = np.empty(n, dtype=np.float32)
        ias_alive_arr = np.zeros(n, dtype=bool)
        comp_fade_arr = np.zeros(n, dtype=np.float32)

        ema_fwd  = 0.0
        ema_lat  = 0.0
        ema_vert = +1.0
        ema_alpha = cfg.accel_ema_alpha
        prev_tas_mps = 0.0
        tasdot_smoothed = 0.0
        comp_fade = 0.0
        comp_step_tau = cfg.comp_fade_tau_sec
        ias_alive = False

        for i in range(n):
            dt = float(dt_arr[i])
            if dt <= 0.0:
                dt = 1.0 / 208.0

            # 1. Installation bias rotation (matches firmware Ahrs.cpp).
            a_fwd_g  = accel_fwd_g[i]
            a_lat_g  = accel_lat_g[i]
            a_vert_g = accel_vert_g[i]
            ax_corr   =  a_fwd_g  *  cp + a_lat_g  * (sr * sp) + a_vert_g * (cr * sp)
            ay_corr   =  a_lat_g  *  cr + a_vert_g * (-sr)
            az_corr   = -a_fwd_g  *  sp + a_lat_g  * (sr * cp) + a_vert_g * (cr * cp)

            # Subtract per-axis gyro biases (from cfg, sourced from
            # the OnSpeed calibration wizard's GX/GY/GZ entries) BEFORE
            # the installation-bias rotation. This is the "static gyro
            # bias the wizard measures on the ground" — distinct from
            # the dynamic gyro bias the EKF's bp/bq/br states might
            # capture. The firmware reads these into config but never
            # applies them; we do, because otherwise the bp/bq/br
            # states have to absorb them and our tight r_bias_prior
            # prevents that.
            g_roll  = float(roll_rate_dps[i])  - cfg.gx_bias_dps
            g_pitch = float(pitch_rate_dps[i]) - cfg.gy_bias_dps
            g_yaw   = float(yaw_rate_dps[i])   - cfg.gz_bias_dps
            roll_rate_corr_dps  =  g_roll *  cp + g_pitch * (sr * sp) + g_yaw * (cr * sp)
            pitch_rate_corr_dps =  g_pitch * cr + g_yaw   * (-sr)
            yaw_rate_corr_dps   = -g_roll *  sp + g_pitch * (sr * cp) + g_yaw * (cp * cr)

            # 2. Accel EMA.
            ema_fwd  += ema_alpha * (ax_corr - ema_fwd)
            ema_lat  += ema_alpha * (ay_corr - ema_lat)
            ema_vert += ema_alpha * (az_corr - ema_vert)

            # 3. TAS + TASdot.
            tas_mps = float(tas_kt[i]) * _KT_TO_MPS
            tasdot_raw = (tas_mps - prev_tas_mps) / dt if dt > 0 else 0.0
            tasdot_smoothed += cfg.tasdot_ema_alpha * (tasdot_raw - tasdot_smoothed)
            prev_tas_mps = tas_mps

            # 4. iasAlive hysteresis + comp fade.
            ias_now = float(ias_kt[i]) if not np.isnan(ias_kt[i]) else 0.0
            if ias_alive:
                if ias_now < cfg.ias_alive_kt - 5.0:
                    ias_alive = False
            else:
                if ias_now > cfg.ias_alive_kt:
                    ias_alive = True
            target = 1.0 if ias_alive else 0.0
            alpha_fade = 1.0 - np.exp(-dt / comp_step_tau)
            comp_fade += alpha_fade * (target - comp_fade)

            # 5. Centripetal + TASdot comp.
            yaw_rate_rps_corr   = np.deg2rad(yaw_rate_corr_dps)
            pitch_rate_rps_corr = np.deg2rad(pitch_rate_corr_dps)
            comp_fwd_g  = tasdot_smoothed / GRAVITY_MPS2
            comp_lat_g  = (tas_mps * yaw_rate_rps_corr)   / GRAVITY_MPS2
            comp_vert_g = (tas_mps * pitch_rate_rps_corr) / GRAVITY_MPS2

            accelFwdComp  = ema_fwd  - comp_fade * comp_fwd_g
            accelLatComp  = ema_lat  - comp_fade * comp_lat_g
            accelVertComp = ema_vert + comp_fade * comp_vert_g

            # 6. Sign flip to standard NED (m/s², rad/s).
            #
            # Gyro signs (verified by VN-300 correlation):
            # • RollRate  : LOGGED is INVERTED from standard → NEGATE.
            # • PitchRate : LOGGED is STANDARD (LogCsv emits −imuPitchRate)
            #               → DO NOT NEGATE. Negating it was a v4–v7 bug.
            # • YawRate   : LOGGED is STANDARD → no flip.
            ax_raw  = +ema_fwd  * GRAVITY_MPS2
            ay_raw  = +ema_lat  * GRAVITY_MPS2
            az_raw  = -ema_vert * GRAVITY_MPS2          # +1g level → −g level
            p_rps =  -np.deg2rad(roll_rate_corr_dps)
            q_rps =  +np.deg2rad(pitch_rate_corr_dps)   # bug fix: no negate
            r_rps =   np.deg2rad(yaw_rate_corr_dps)

            # 7. EKF predict (raw smoothed accel — centripetal handled in
            #    correct()'s h(x), so we no longer pre-subtract it).
            self.ekf.predict(p_rps, q_rps, r_rps, ax_raw, ay_raw, az_raw,
                             tas_mps, dt)

            # 8. EKF correct. We pass RAW (no centripetal-subtraction)
            #    accel and let h(x) inside the EKF model centripetal +
            #    TASdot using the state's bias-corrected gyros and TAS.
            #    The new ∂h_ay/∂br and ∂h_az/∂bq Jacobian entries couple
            #    lateral/vertical accel residuals to the gyro bias states.
            #
            #    Centripetal + TASdot inputs are gated by comp_fade (the
            #    same iasAlive ramp-in coefficient we use for the legacy
            #    upstream-compensation path). When the aircraft is on the
            #    ground IAS noise floor → tasdot has spurious spikes from
            #    pressure-sensor jitter that, fed through h_ax, were
            #    pulling EKF pitch ~2° below truth. comp_fade ≈ 0 on the
            #    ground silences those terms; once airborne the fade
            #    ramps to 1 and the centripetal terms re-enable smoothly.
            baro_z = float(palt_ft[i]) * _FT_TO_M
            # tas_mps and tasdot are FADED by comp_fade so they zero out
            # on the ground (pressure-sensor noise floor on IAS produced
            # bogus tasdot spikes that biased EKF pitch). Gyro rates stay
            # un-faded — they only enter h(x) multiplied by tas_mps, so
            # fading tas alone is sufficient to silence centripetal terms
            # on the ground.
            self.ekf.correct(ax_raw, ay_raw, az_raw, baro_z,
                             tas_mps      = tas_mps * comp_fade,
                             pitch_rate_rps = q_rps,
                             yaw_rate_rps   = r_rps,
                             tasdot_mps2  = tasdot_smoothed * comp_fade,
                             update_baro=True, update_beta_prior=True)

            # 9. Record outputs.
            s = self.ekf.state
            pitch_deg[i] = s.pitch_deg
            roll_deg[i]  = s.roll_deg
            yaw_deg[i]   = s.yaw_deg
            beta_deg[i]  = s.beta_deg
            vz_mps[i]    = s.vz
            z_m[i]       = s.z
            bp_dps[i]    = np.rad2deg(s.bp)
            bq_dps[i]    = np.rad2deg(s.bq)
            br_dps[i]    = np.rad2deg(s.br)
            b_az_mps2[i] = s.b_az
            # Derived kinematic AOA from full β-aware formula
            alpha_deg[i] = np.rad2deg(alpha_kinematic(
                s.roll_rad, s.pitch_rad, s.vz, tas_mps, s.beta))
            ias_alive_arr[i] = ias_alive
            comp_fade_arr[i] = comp_fade

        self.history = {
            "pitch_deg": pitch_deg, "roll_deg": roll_deg, "yaw_deg": yaw_deg,
            "alpha_deg": alpha_deg, "beta_deg": beta_deg,
            "vz_mps": vz_mps, "z_m": z_m,
            "bp_dps": bp_dps, "bq_dps": bq_dps, "br_dps": br_dps,
            "b_az_mps2": b_az_mps2,
            "ias_alive": ias_alive_arr, "comp_fade": comp_fade_arr,
        }
        return self.history
