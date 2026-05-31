// EKFQ.cpp — embedded-optimised 11-state quaternion EKF.
//
// The ESP32-S3 in the OnSpeed Gen3 box runs WiFi, two serial streams, SD
// logging, pressure ADC, audio mixing, and several display tasks alongside
// the AHRS, so every cycle saved here matters. Expensive paths are kept
// in sparse-aware specialised forms:
//
//   • Predict: F has identity baseline plus sparse perturbations. Four
//     rows (bp/bq/br/b_az) and conditionally a fifth (BETA when TAS is
//     below the dynamics gate) are PURE identity. We never materialise
//     F as a 11×11 matrix — F·P·F^T is computed directly using just the
//     ≤44 sparse perturbation entries on the remaining rows. Roughly
//     2× faster than naïve 2·N³ for N=11.
//
//   • Correct: pure BATCH update that mirrors the Python reference
//     (onspeed_ekf/ekf_quat.py) exactly. All residuals are computed at
//     the predict state, a single joint Kalman gain K is solved via
//     Cholesky factorisation of S = H·P·Hᵀ + R (SPD by construction),
//     and the covariance is updated in Joseph form. This is the same
//     algorithm whose parameters were Optuna-tuned against VN-300 truth
//     — a sequential approximation would not be numerically identical
//     and would not honour the tuning's intent.
//
//   • All memory is stack — no heap, no allocators, no exceptions.

#include "EKFQ.h"

#include <cmath>
#include <cstring>

namespace onspeed {

// ---------------------------------------------------------------------------
// Defaults — Optuna study ekfq_v15 best (trial #146).
// Will be re-locked to v16 best once that run finishes.
// ---------------------------------------------------------------------------

EKFQ::Config EKFQ::Config::defaults() {
    Config c{};
    c.q_quat       = 1.3876969636637712e-06f;
    c.q_bias       = 0.08059835153457112f;
    c.q_z          = 0.000760923735397084f;
    c.q_vz         = 0.00011863168292171215f;
    c.q_b_az       = 0.00011126568311539455f;
    c.q_beta       = 3.525093877403027e-08f;
    c.r_ax         = 16.594799559856998f;
    c.r_ay         = 10.855551467684359f;
    c.r_az         = 12.758742327066178f;
    c.r_baro       = 5.015458731982534f;
    c.r_beta_prior = 0.13441309019767658f;
    c.r_bias_prior = 0.00035857919885685886f;
    c.k_beta_R     = 6.79749216596058f;
    // Initial covariance.
    c.p_quat       = 0.05f;
    c.p_bias       = 0.01f;
    c.p_z          = 100.0f;
    c.p_vz         = 4.0f;
    c.p_b_az       = 1.0f;
    c.p_beta       = 0.01f;
    c.tas_min_mps  = 12.0f;
    return c;
}

// ---------------------------------------------------------------------------
// Quaternion / Euler helpers
// ---------------------------------------------------------------------------

void EKFQ::eulerToQuat(float roll, float pitch, float yaw, float out[4]) {
    const float cy = std::cos(yaw * 0.5f);
    const float sy = std::sin(yaw * 0.5f);
    const float cp = std::cos(pitch * 0.5f);
    const float sp = std::sin(pitch * 0.5f);
    const float cr = std::cos(roll * 0.5f);
    const float sr = std::sin(roll * 0.5f);
    out[0] = cr * cp * cy + sr * sp * sy;
    out[1] = sr * cp * cy - cr * sp * sy;
    out[2] = cr * sp * cy + sr * cp * sy;
    out[3] = cr * cp * sy - sr * sp * cy;
}

float EKFQ::State::roll_rad() const {
    return std::atan2(2.0f * (q0 * q1 + q2 * q3),
                      1.0f - 2.0f * (q1 * q1 + q2 * q2));
}

float EKFQ::State::pitch_rad() const {
    float s = 2.0f * (q0 * q2 - q3 * q1);
    if (s >  1.0f) s =  1.0f;
    if (s < -1.0f) s = -1.0f;
    return std::asin(s);
}

float EKFQ::State::yaw_rad() const {
    return std::atan2(2.0f * (q0 * q3 + q1 * q2),
                      1.0f - 2.0f * (q2 * q2 + q3 * q3));
}

float EKFQ::State::roll_deg()  const { return roll_rad()  * 57.29577951308232f; }
float EKFQ::State::pitch_deg() const { return pitch_rad() * 57.29577951308232f; }
float EKFQ::State::yaw_deg()   const { return yaw_rad()   * 57.29577951308232f; }
float EKFQ::State::beta_deg()  const { return beta * 57.29577951308232f; }

// ---------------------------------------------------------------------------
// Construction / init / reset
// ---------------------------------------------------------------------------

EKFQ::EKFQ() : config_(Config::defaults()), initialized_(false) {
    init();
}

EKFQ::EKFQ(const Config& cfg) : config_(cfg), initialized_(false) {
    init();
}

void EKFQ::init(float initial_phi, float initial_theta, float initial_z) {
    std::memset(x_, 0, sizeof(x_));
    float q[4];
    eulerToQuat(initial_phi, initial_theta, 0.0f, q);
    x_[Q0] = q[0]; x_[Q1] = q[1]; x_[Q2] = q[2]; x_[Q3] = q[3];
    x_[Z] = initial_z;
    std::memset(P_, 0, sizeof(P_));
    P_[Q0][Q0] = config_.p_quat;
    P_[Q1][Q1] = config_.p_quat;
    P_[Q2][Q2] = config_.p_quat;
    P_[Q3][Q3] = config_.p_quat;
    P_[BP_IDX][BP_IDX] = config_.p_bias;
    P_[BQ_IDX][BQ_IDX] = config_.p_bias;
    P_[BR_IDX][BR_IDX] = config_.p_bias;
    P_[Z][Z]   = config_.p_z;
    P_[VZ][VZ] = config_.p_vz;
    P_[B_AZ][B_AZ] = config_.p_b_az;
    P_[BETA][BETA] = config_.p_beta;
    initialized_ = true;
}

void EKFQ::resetVerticalCovariance(float baro_z) {
    if (!initialized_) return;
    // Zero out all rows/cols for z, vz, b_az (clearing cross-correlations
    // built up while the baro was stale), then reseed their diagonals.
    static constexpr int kIdx[3] = { Z, VZ, B_AZ };
    for (int n = 0; n < 3; ++n) {
        const int idx = kIdx[n];
        for (int i = 0; i < N_STATES; ++i) {
            P_[idx][i] = 0.0f;
            P_[i][idx] = 0.0f;
        }
    }
    P_[Z][Z]      = config_.p_z;
    P_[VZ][VZ]    = config_.p_vz;
    P_[B_AZ][B_AZ] = config_.p_b_az;
    x_[Z] = baro_z;
}

void EKFQ::renormaliseQuaternion() {
    const float n2 = x_[Q0]*x_[Q0] + x_[Q1]*x_[Q1]
                   + x_[Q2]*x_[Q2] + x_[Q3]*x_[Q3];
    if (n2 > 0.0f) {
        const float inv = 1.0f / std::sqrt(n2);
        x_[Q0] *= inv; x_[Q1] *= inv;
        x_[Q2] *= inv; x_[Q3] *= inv;
    }
}

// (No file-scope helpers needed — correct() does a pure batch update
// with Cholesky-based joint Kalman gain, mirroring Python exactly.
// See the Correct step below for the algebra and Cholesky implementation.)

// ---------------------------------------------------------------------------
// update = predict + correct
// ---------------------------------------------------------------------------

void EKFQ::update(const Measurements& m, float dt) {
    if (!initialized_) init();
    predict(m.p, m.q, m.r, m.ax, m.ay, m.az, m.tasMps, dt);
    correct(m.ax, m.ay, m.az, m.tasMps, m.tasDotMps2,
            m.q, m.r, m.baroAltMeters, m.updateBaro);
}

// ---------------------------------------------------------------------------
// Predict step — sparse F·P·F^T propagation
// ---------------------------------------------------------------------------

void EKFQ::predict(float p, float q_in, float r_in,
                   float ax_raw, float ay_raw, float az_raw,
                   float tas, float dt) {
    // Read state into locals once.
    const float q0 = x_[Q0], q1 = x_[Q1], q2 = x_[Q2], q3 = x_[Q3];
    const float bp = x_[BP_IDX], bq = x_[BQ_IDX], br = x_[BR_IDX];
    const float vz = x_[VZ], b_az = x_[B_AZ], beta = x_[BETA];

    // Bias-corrected gyros.
    const float p_c = p   - bp;
    const float q_c = q_in - bq;
    const float r_c = r_in - br;

    // 1) Quaternion mean propagation by the exact exponential map.
    //
    // For a body rate ω = (p_c, q_c, r_c) held constant across the step, the
    // attitude advances by q ← q ⊗ exp(½ ω dt), where the increment is the
    // unit quaternion of the rotation vector ω·dt:
    //
    //     Δq = [ cos(½‖ω‖dt),  sinc(½‖ω‖dt) · ½ ω dt ]
    //
    // sinc(x) = sin(x)/x → 1 as x → 0, so the small-angle limit is the
    // first-order step Δq ≈ [1, ½ ω dt] — the same q̇ = ½ Ω(ω) q the linear
    // covariance Jacobian F below is built from. Unlike q + q̇·dt, Δq is unit
    // by construction and the Hamilton product preserves ‖q‖, so the mean no
    // longer relies on the post-step renormalise to undo integration drift.
    const float half_dt = 0.5f * dt;
    const float wx = p_c * dt, wy = q_c * dt, wz = r_c * dt;  // rotation vector
    const float theta2 = wx * wx + wy * wy + wz * wz;         // ‖ω·dt‖²
    const float half_angle = 0.5f * std::sqrt(theta2);
    const float dq_w = std::cos(half_angle);
    // s = sinc(½‖ω‖dt) · ½  — the vector-part scale on ω·dt. Taylor-expand
    // sinc near zero to stay exact and branch-cheap through ω → 0.
    float s;
    if (half_angle > 1e-4f) {
        s = 0.5f * std::sin(half_angle) / half_angle;
    } else {
        // sinc(x) ≈ 1 − x²/6; here x = half_angle, so ½·sinc ≈ ½(1 − x²/6).
        s = 0.5f * (1.0f - half_angle * half_angle * (1.0f / 6.0f));
    }
    const float dq_x = s * wx, dq_y = s * wy, dq_z = s * wz;

    // Hamilton product q_new = q ⊗ Δq. Same operand ordering as the linear
    // step it replaces; to first order in ω·dt this reduces to q + q̇·dt.
    const float q0_new = q0 * dq_w - q1 * dq_x - q2 * dq_y - q3 * dq_z;
    const float q1_new = q0 * dq_x + q1 * dq_w + q2 * dq_z - q3 * dq_y;
    const float q2_new = q0 * dq_y - q1 * dq_z + q2 * dq_w + q3 * dq_x;
    const float q3_new = q0 * dq_z + q1 * dq_y - q2 * dq_x + q3 * dq_w;

    // 2) Body-frame gravity components (R₂₀, R₂₁, R₂₂ = third row of R_be).
    const float R20 = 2.0f * (q1 * q3 - q0 * q2);
    const float R21 = 2.0f * (q2 * q3 + q0 * q1);
    const float R22 = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

    // 3) Earth-down accel and vertical channel update.
    const float a_D    = R20 * ax_raw + R21 * ay_raw + R22 * az_raw
                         + GRAVITY - b_az;
    const float new_z  = x_[Z] - (vz + 0.5f * a_D * dt) * dt;
    const float new_vz = vz + a_D * dt;

    // 4) β dynamics — α ≈ 0 simplification, gated by TAS.
    // β̇ ≈ (ay_raw + g·R21) / TAS − r_c
    const bool tas_active = (tas > config_.tas_min_mps);
    float new_beta = beta;
    if (tas_active) {
        const float g_body_y = R21 * GRAVITY;
        const float beta_dot = (ay_raw + g_body_y) / tas - r_c;
        new_beta = beta + dt * beta_dot;
    }

    // 5) Compute the sparse F = I + δF perturbation coefficients.
    //
    // Rows that are PURE IDENTITY: BP_IDX, BQ_IDX, BR_IDX, B_AZ (and BETA when !tas_active).
    // All other rows have a small fixed pattern of δF entries:
    //
    //   Q0 row: 7 δF entries  (Q1, Q2, Q3 quaternion-block + BP_IDX, BQ_IDX, BR_IDX bias)
    //   Q1 row: 7
    //   Q2 row: 7
    //   Q3 row: 7
    //   Z  row: 6  (Q0..Q3 quat + VZ + B_AZ)
    //   VZ row: 5  (Q0..Q3 quat + B_AZ)
    //   BETA row: 5  (Q0..Q3 quat + BR_IDX)   — only when tas_active

    // Quaternion-quaternion block: ∂q̇_i/∂q_j · dt  (entries on rows Q0..Q3).
    const float a01 = -half_dt * p_c;   // F[Q0][Q1]
    const float a02 = -half_dt * q_c;   // F[Q0][Q2]
    const float a03 = -half_dt * r_c;   // F[Q0][Q3]
    const float a10 =  half_dt * p_c;   // F[Q1][Q0]
    const float a12 =  half_dt * r_c;   // F[Q1][Q2]
    const float a13 = -half_dt * q_c;   // F[Q1][Q3]
    const float a20 =  half_dt * q_c;   // F[Q2][Q0]
    const float a21 = -half_dt * r_c;   // F[Q2][Q1]
    const float a23 =  half_dt * p_c;   // F[Q2][Q3]
    const float a30 =  half_dt * r_c;   // F[Q3][Q0]
    const float a31 =  half_dt * q_c;   // F[Q3][Q1]
    const float a32 = -half_dt * p_c;   // F[Q3][Q2]

    // Quaternion-bias block: ∂q̇_i/∂b_j · dt.
    // Sign comes from b_j entering as ω = ω_meas − b.
    const float qb_q0_bp =  half_dt * q1, qb_q0_bq =  half_dt * q2, qb_q0_br =  half_dt * q3;
    const float qb_q1_bp = -half_dt * q0, qb_q1_bq =  half_dt * q3, qb_q1_br = -half_dt * q2;
    const float qb_q2_bp = -half_dt * q3, qb_q2_bq = -half_dt * q0, qb_q2_br =  half_dt * q1;
    const float qb_q3_bp =  half_dt * q2, qb_q3_bq = -half_dt * q1, qb_q3_br = -half_dt * q0;

    // Partials of a_D w.r.t. quaternion (for Z/VZ rows).
    const float dD_q0 = -2.0f * q2 * ax_raw + 2.0f * q1 * ay_raw + 2.0f * q0 * az_raw;
    const float dD_q1 =  2.0f * q3 * ax_raw + 2.0f * q0 * ay_raw - 2.0f * q1 * az_raw;
    const float dD_q2 = -2.0f * q0 * ax_raw + 2.0f * q3 * ay_raw - 2.0f * q2 * az_raw;
    const float dD_q3 =  2.0f * q1 * ax_raw + 2.0f * q2 * ay_raw + 2.0f * q3 * az_raw;
    const float dt2  = dt * dt;
    const float half_dt2 = 0.5f * dt2;

    const float vz_q0 = dt * dD_q0, vz_q1 = dt * dD_q1;
    const float vz_q2 = dt * dD_q2, vz_q3 = dt * dD_q3;
    const float vz_baz = -dt;
    const float z_q0 = -half_dt2 * dD_q0, z_q1 = -half_dt2 * dD_q1;
    const float z_q2 = -half_dt2 * dD_q2, z_q3 = -half_dt2 * dD_q3;
    const float z_vz = -dt, z_baz = half_dt2;

    // β row coefficients (only when tas_active).
    float beta_q0 = 0.0f, beta_q1 = 0.0f, beta_q2 = 0.0f, beta_q3 = 0.0f;
    float beta_br_dt = 0.0f;
    if (tas_active) {
        const float g_over_tas_dt = (GRAVITY / tas) * dt;
        beta_q0 = g_over_tas_dt * 2.0f * q1;
        beta_q1 = g_over_tas_dt * 2.0f * q0;
        beta_q2 = g_over_tas_dt * 2.0f * q3;
        beta_q3 = g_over_tas_dt * 2.0f * q2;
        beta_br_dt = dt;
    }

    // 6) Compute M = F · P column-by-column into buffers, only for rows
    //    where F deviates from identity. Identity rows of F (BP_IDX, BQ_IDX, BR_IDX,
    //    B_AZ, and possibly BETA) trivially give M[i][j] = P[i][j].

    float M_Q0[N_STATES], M_Q1[N_STATES], M_Q2[N_STATES], M_Q3[N_STATES];
    float M_Z[N_STATES], M_VZ[N_STATES], M_BETA[N_STATES];
    for (int j = 0; j < N_STATES; ++j) {
        const float Pq0 = P_[Q0][j], Pq1 = P_[Q1][j];
        const float Pq2 = P_[Q2][j], Pq3 = P_[Q3][j];
        const float Pbp = P_[BP_IDX][j], Pbq = P_[BQ_IDX][j], Pbr = P_[BR_IDX][j];
        const float Pvz = P_[VZ][j], Pbaz = P_[B_AZ][j];

        M_Q0[j] = Pq0 + a01 * Pq1 + a02 * Pq2 + a03 * Pq3
                + qb_q0_bp * Pbp + qb_q0_bq * Pbq + qb_q0_br * Pbr;
        M_Q1[j] = Pq1 + a10 * Pq0 + a12 * Pq2 + a13 * Pq3
                + qb_q1_bp * Pbp + qb_q1_bq * Pbq + qb_q1_br * Pbr;
        M_Q2[j] = Pq2 + a20 * Pq0 + a21 * Pq1 + a23 * Pq3
                + qb_q2_bp * Pbp + qb_q2_bq * Pbq + qb_q2_br * Pbr;
        M_Q3[j] = Pq3 + a30 * Pq0 + a31 * Pq1 + a32 * Pq2
                + qb_q3_bp * Pbp + qb_q3_bq * Pbq + qb_q3_br * Pbr;

        M_VZ[j] = Pvz + vz_q0 * Pq0 + vz_q1 * Pq1
                       + vz_q2 * Pq2 + vz_q3 * Pq3
                       + vz_baz * Pbaz;
        M_Z[j]  = P_[Z][j] + z_q0 * Pq0 + z_q1 * Pq1
                           + z_q2 * Pq2 + z_q3 * Pq3
                           + z_vz * Pvz + z_baz * Pbaz;

        if (tas_active) {
            M_BETA[j] = P_[BETA][j] + beta_q0 * Pq0 + beta_q1 * Pq1
                                    + beta_q2 * Pq2 + beta_q3 * Pq3
                                    + beta_br_dt * Pbr;
        } else {
            M_BETA[j] = P_[BETA][j];
        }
    }
    // Identity rows of F: M[i][j] = P[i][j] (read directly when needed).

    // 7) Compute P_new = M · F^T using the same sparse structure.
    //    For each output element P_new[i][j], we need M[i][k] · F[j][k].
    //    F[j][k] = δ_jk + δF[j][k]; the identity contribution gives
    //    M[i][j], and the δF perturbations add a few terms per column.
    //
    //    We cache M's row i values once per outer iteration, then compute
    //    all 11 columns of P_new[i][:]. Writes into P_ are safe because
    //    we cache the row's M values BEFORE writing back.

    for (int i = 0; i < N_STATES; ++i) {
        // Cache M[i][:] into locals. Source is one of M_* buffers (for
        // non-identity rows) or P_[i][:] (for identity rows).
        float Mi_Q0, Mi_Q1, Mi_Q2, Mi_Q3;
        float Mi_BP, Mi_BQ, Mi_BR;
        float Mi_Z,  Mi_VZ, Mi_BAZ;
        float Mi_BETA;
        switch (i) {
            case Q0:
                Mi_Q0 = M_Q0[Q0]; Mi_Q1 = M_Q0[Q1]; Mi_Q2 = M_Q0[Q2]; Mi_Q3 = M_Q0[Q3];
                Mi_BP = M_Q0[BP_IDX]; Mi_BQ = M_Q0[BQ_IDX]; Mi_BR = M_Q0[BR_IDX];
                Mi_Z  = M_Q0[Z];  Mi_VZ = M_Q0[VZ]; Mi_BAZ = M_Q0[B_AZ];
                Mi_BETA = M_Q0[BETA];
                break;
            case Q1:
                Mi_Q0 = M_Q1[Q0]; Mi_Q1 = M_Q1[Q1]; Mi_Q2 = M_Q1[Q2]; Mi_Q3 = M_Q1[Q3];
                Mi_BP = M_Q1[BP_IDX]; Mi_BQ = M_Q1[BQ_IDX]; Mi_BR = M_Q1[BR_IDX];
                Mi_Z  = M_Q1[Z];  Mi_VZ = M_Q1[VZ]; Mi_BAZ = M_Q1[B_AZ];
                Mi_BETA = M_Q1[BETA];
                break;
            case Q2:
                Mi_Q0 = M_Q2[Q0]; Mi_Q1 = M_Q2[Q1]; Mi_Q2 = M_Q2[Q2]; Mi_Q3 = M_Q2[Q3];
                Mi_BP = M_Q2[BP_IDX]; Mi_BQ = M_Q2[BQ_IDX]; Mi_BR = M_Q2[BR_IDX];
                Mi_Z  = M_Q2[Z];  Mi_VZ = M_Q2[VZ]; Mi_BAZ = M_Q2[B_AZ];
                Mi_BETA = M_Q2[BETA];
                break;
            case Q3:
                Mi_Q0 = M_Q3[Q0]; Mi_Q1 = M_Q3[Q1]; Mi_Q2 = M_Q3[Q2]; Mi_Q3 = M_Q3[Q3];
                Mi_BP = M_Q3[BP_IDX]; Mi_BQ = M_Q3[BQ_IDX]; Mi_BR = M_Q3[BR_IDX];
                Mi_Z  = M_Q3[Z];  Mi_VZ = M_Q3[VZ]; Mi_BAZ = M_Q3[B_AZ];
                Mi_BETA = M_Q3[BETA];
                break;
            case Z:
                Mi_Q0 = M_Z[Q0]; Mi_Q1 = M_Z[Q1]; Mi_Q2 = M_Z[Q2]; Mi_Q3 = M_Z[Q3];
                Mi_BP = M_Z[BP_IDX]; Mi_BQ = M_Z[BQ_IDX]; Mi_BR = M_Z[BR_IDX];
                Mi_Z  = M_Z[Z];  Mi_VZ = M_Z[VZ]; Mi_BAZ = M_Z[B_AZ];
                Mi_BETA = M_Z[BETA];
                break;
            case VZ:
                Mi_Q0 = M_VZ[Q0]; Mi_Q1 = M_VZ[Q1]; Mi_Q2 = M_VZ[Q2]; Mi_Q3 = M_VZ[Q3];
                Mi_BP = M_VZ[BP_IDX]; Mi_BQ = M_VZ[BQ_IDX]; Mi_BR = M_VZ[BR_IDX];
                Mi_Z  = M_VZ[Z];  Mi_VZ = M_VZ[VZ]; Mi_BAZ = M_VZ[B_AZ];
                Mi_BETA = M_VZ[BETA];
                break;
            case BETA:
                Mi_Q0 = M_BETA[Q0]; Mi_Q1 = M_BETA[Q1]; Mi_Q2 = M_BETA[Q2]; Mi_Q3 = M_BETA[Q3];
                Mi_BP = M_BETA[BP_IDX]; Mi_BQ = M_BETA[BQ_IDX]; Mi_BR = M_BETA[BR_IDX];
                Mi_Z  = M_BETA[Z];  Mi_VZ = M_BETA[VZ]; Mi_BAZ = M_BETA[B_AZ];
                Mi_BETA = M_BETA[BETA];
                break;
            default:  // BP_IDX, BQ_IDX, BR_IDX, B_AZ — identity rows of F
                Mi_Q0 = P_[i][Q0]; Mi_Q1 = P_[i][Q1]; Mi_Q2 = P_[i][Q2]; Mi_Q3 = P_[i][Q3];
                Mi_BP = P_[i][BP_IDX]; Mi_BQ = P_[i][BQ_IDX]; Mi_BR = P_[i][BR_IDX];
                Mi_Z  = P_[i][Z];  Mi_VZ = P_[i][VZ]; Mi_BAZ = P_[i][B_AZ];
                Mi_BETA = P_[i][BETA];
                break;
        }

        // Now write the 11 columns of P_new[i][:] = M[i][:] · F^T.
        // Identity columns (BP_IDX/BQ_IDX/BR_IDX/B_AZ): just write the cached M value.
        P_[i][BP_IDX]   = Mi_BP;
        P_[i][BQ_IDX]   = Mi_BQ;
        P_[i][BR_IDX]   = Mi_BR;
        P_[i][B_AZ] = Mi_BAZ;

        // Q-block columns: each adds the row-j perturbations of F.
        P_[i][Q0] = Mi_Q0 + a01 * Mi_Q1 + a02 * Mi_Q2 + a03 * Mi_Q3
                          + qb_q0_bp * Mi_BP + qb_q0_bq * Mi_BQ + qb_q0_br * Mi_BR;
        P_[i][Q1] = Mi_Q1 + a10 * Mi_Q0 + a12 * Mi_Q2 + a13 * Mi_Q3
                          + qb_q1_bp * Mi_BP + qb_q1_bq * Mi_BQ + qb_q1_br * Mi_BR;
        P_[i][Q2] = Mi_Q2 + a20 * Mi_Q0 + a21 * Mi_Q1 + a23 * Mi_Q3
                          + qb_q2_bp * Mi_BP + qb_q2_bq * Mi_BQ + qb_q2_br * Mi_BR;
        P_[i][Q3] = Mi_Q3 + a30 * Mi_Q0 + a31 * Mi_Q1 + a32 * Mi_Q2
                          + qb_q3_bp * Mi_BP + qb_q3_bq * Mi_BQ + qb_q3_br * Mi_BR;

        // VZ column.
        P_[i][VZ] = Mi_VZ + vz_q0 * Mi_Q0 + vz_q1 * Mi_Q1
                          + vz_q2 * Mi_Q2 + vz_q3 * Mi_Q3
                          + vz_baz * Mi_BAZ;
        // Z column.
        P_[i][Z]  = Mi_Z  + z_q0 * Mi_Q0 + z_q1 * Mi_Q1
                          + z_q2 * Mi_Q2 + z_q3 * Mi_Q3
                          + z_vz * Mi_VZ + z_baz * Mi_BAZ;
        // BETA column.
        if (tas_active) {
            P_[i][BETA] = Mi_BETA + beta_q0 * Mi_Q0 + beta_q1 * Mi_Q1
                                  + beta_q2 * Mi_Q2 + beta_q3 * Mi_Q3
                                  + beta_br_dt * Mi_BR;
        } else {
            P_[i][BETA] = Mi_BETA;
        }
    }

    // 8) Add Q·dt on the diagonal.
    P_[Q0][Q0] += config_.q_quat * dt;
    P_[Q1][Q1] += config_.q_quat * dt;
    P_[Q2][Q2] += config_.q_quat * dt;
    P_[Q3][Q3] += config_.q_quat * dt;
    P_[BP_IDX][BP_IDX] += config_.q_bias * dt;
    P_[BQ_IDX][BQ_IDX] += config_.q_bias * dt;
    P_[BR_IDX][BR_IDX] += config_.q_bias * dt;
    P_[Z][Z]   += config_.q_z    * dt;
    P_[VZ][VZ] += config_.q_vz   * dt;
    P_[B_AZ][B_AZ] += config_.q_b_az * dt;
    P_[BETA][BETA] += config_.q_beta * dt;

    // 9) Commit state.
    x_[Q0] = q0_new;
    x_[Q1] = q1_new;
    x_[Q2] = q2_new;
    x_[Q3] = q3_new;
    x_[Z]    = new_z;
    x_[VZ]   = new_vz;
    x_[BETA] = new_beta;
    // The exponential-map increment is unit by construction, so this only
    // sheds the float rounding that accumulates over millions of products.
    renormaliseQuaternion();
}

// ---------------------------------------------------------------------------
// Correct step — pure BATCH update, byte-for-byte equivalent to the
// Python reference `onspeed_ekf/ekf_quat.py::EKFQ.correct()` that produced
// the Optuna-tuned defaults.
//
// Algorithm (mirrors Python lines 417-547):
//   1) Snapshot the predict-state quaternion / biases / β / z.
//   2) Build the full measurement set:
//        z_meas = [ax_meas, ay_meas, az_meas, baro_z?, 0(β), 0(bp), 0(bq), 0(br)]
//        h(x_pred) = predicted equivalents using the snapshot
//        H = 8×11 measurement Jacobian (sparse, but written explicitly)
//        R_diag = per-measurement variance, with R_ay inflated by
//                 (1 + k_beta_R · β²) — the β-adaptive trick.
//   3) Compute innovation y = z_meas − h(x_pred)  (all at predict state).
//   4) Compute PHt = P · Hᵀ                       (11×8).
//   5) Compute S = H · PHt + R_diag               (8×8, SPD).
//   6) Cholesky-factorise S = L·Lᵀ (in-place lower triangle).
//   7) Solve for K: row-by-row triangular solve   (11 systems, one per row).
//   8) Single state update x = x + K · y.
//   9) Joseph-form covariance P = (I − K·H)·P·(I − K·H)ᵀ + K·R·Kᵀ
//      (numerically safer than the simple form under fp32 round-off; the
//      Python reference also uses Joseph form).
//  10) Renormalise the quaternion.
//
// All buffers are stack-allocated and sized for the maximum 8-measurement
// case. Total ~2.4 KB of stack frame; <8 KB total task stack budget on
// the ESP32-S3 still has headroom.
//
// If the baro update is requested but `updateBaro=false`, we collapse to
// 7 active measurements (the baro row is omitted from H/y/R entirely).
// ---------------------------------------------------------------------------

void EKFQ::correct(float ax_meas, float ay_meas, float az_meas,
                   float tas, float tasDot,
                   float pitchRate, float yawRate,
                   float baroZ, bool updateBaro) {
    // 1) Snapshot predict state.
    const float q0 = x_[Q0], q1 = x_[Q1], q2 = x_[Q2], q3 = x_[Q3];
    const float bp = x_[BP_IDX], bq = x_[BQ_IDX], br = x_[BR_IDX];
    const float z = x_[Z], beta = x_[BETA];
    const float g = GRAVITY;

    // Bias-corrected gyros for the centripetal terms inside h(x).
    const float q_c = pitchRate - bq;
    const float r_c = yawRate   - br;

    // Predicted body specific force = gravity contribution + centripetal +
    // TASdot. These three terms exactly match the Python reference.
    const float ax_pred = -2.0f * g * (q1 * q3 - q0 * q2) + tasDot;
    const float ay_pred = -2.0f * g * (q2 * q3 + q0 * q1) + tas * r_c;
    const float az_pred =       -g * (q0*q0 - q1*q1 - q2*q2 + q3*q3) - tas * q_c;

    // β-adaptive R inflation: R_ay grows quadratically in β so the filter
    // de-weights lateral-G during sustained slips (when gravity-only
    // lateral-G is no longer a clean attitude reference).
    const float r_ay_eff = config_.r_ay
                           * (1.0f + config_.k_beta_R * beta * beta);

    // 2) Build measurement vectors and H matrix (8 max, 7 when !updateBaro).
    constexpr int N_MEAS_MAX = 8;
    float z_meas [N_MEAS_MAX];
    float h_pred [N_MEAS_MAX];
    float R_diag [N_MEAS_MAX];
    float H[N_MEAS_MAX][N_STATES];
    std::memset(H, 0, sizeof(H));

    int n = 0;
    // accel x  (4 quaternion-block non-zeros)
    z_meas[n] = ax_meas; h_pred[n] = ax_pred; R_diag[n] = config_.r_ax;
    H[n][Q0] =  2.0f * g * q2;
    H[n][Q1] = -2.0f * g * q3;
    H[n][Q2] =  2.0f * g * q0;
    H[n][Q3] = -2.0f * g * q1;
    n++;
    // accel y  (4 quat + BR_IDX centripetal coupling)
    z_meas[n] = ay_meas; h_pred[n] = ay_pred; R_diag[n] = r_ay_eff;
    H[n][Q0] = -2.0f * g * q1;
    H[n][Q1] = -2.0f * g * q0;
    H[n][Q2] = -2.0f * g * q3;
    H[n][Q3] = -2.0f * g * q2;
    H[n][BR_IDX] = -tas;
    n++;
    // accel z  (4 quat + BQ_IDX centripetal coupling)
    z_meas[n] = az_meas; h_pred[n] = az_pred; R_diag[n] = config_.r_az;
    H[n][Q0] = -2.0f * g * q0;
    H[n][Q1] =  2.0f * g * q1;
    H[n][Q2] =  2.0f * g * q2;
    H[n][Q3] = -2.0f * g * q3;
    H[n][BQ_IDX] = +tas;
    n++;
    // baro altitude (optional)
    if (updateBaro) {
        z_meas[n] = baroZ; h_pred[n] = z; R_diag[n] = config_.r_baro;
        H[n][Z] = 1.0f;
        n++;
    }
    // β = 0 weak prior
    z_meas[n] = 0.0f; h_pred[n] = beta; R_diag[n] = config_.r_beta_prior;
    H[n][BETA] = 1.0f;
    n++;
    // Gyro-bias = 0 weak priors (read from snapshot, NOT from x_, so the
    // residual sees the predict-state values just like Python does).
    z_meas[n] = 0.0f; h_pred[n] = bp; R_diag[n] = config_.r_bias_prior;
    H[n][BP_IDX] = 1.0f;
    n++;
    z_meas[n] = 0.0f; h_pred[n] = bq; R_diag[n] = config_.r_bias_prior;
    H[n][BQ_IDX] = 1.0f;
    n++;
    z_meas[n] = 0.0f; h_pred[n] = br; R_diag[n] = config_.r_bias_prior;
    H[n][BR_IDX] = 1.0f;
    n++;

    // 3) Innovation y = z_meas − h_pred (all at predict state).
    float y[N_MEAS_MAX];
    for (int i = 0; i < n; ++i) y[i] = z_meas[i] - h_pred[i];

    // 4) PHt = P · Hᵀ  (N_STATES × n). H rows are sparse but we just
    //    use the obvious triple loop — overall O(N·N·n) ≈ 1000 flops.
    float PHt[N_STATES][N_MEAS_MAX];
    for (int i = 0; i < N_STATES; ++i) {
        for (int j = 0; j < n; ++j) {
            float s = 0.0f;
            for (int k = 0; k < N_STATES; ++k) {
                s += P_[i][k] * H[j][k];   // Hᵀ[k][j] = H[j][k]
            }
            PHt[i][j] = s;
        }
    }

    // 5) S = H · PHt + R  (n × n, symmetric positive definite by
    //    construction: P is SPD, R is positive diagonal). Compute the
    //    lower triangle and mirror.
    float S[N_MEAS_MAX][N_MEAS_MAX];
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            float s = 0.0f;
            for (int k = 0; k < N_STATES; ++k) s += H[i][k] * PHt[k][j];
            S[i][j] = s;
            if (i != j) S[j][i] = s;
        }
        S[i][i] += R_diag[i];
    }

    // 6) Cholesky factorise S in-place. L overwrites lower triangle of S;
    //    upper triangle becomes scratch (not read after this point).
    for (int j = 0; j < n; ++j) {
        float sum = S[j][j];
        for (int k = 0; k < j; ++k) sum -= S[j][k] * S[j][k];
        if (sum <= 0.0f) {
            // S lost positive-definiteness due to fp32 round-off. Abort
            // the update — same failure mode as the Python try/except
            // around np.linalg.solve (LinAlgError on a singular S).
            return;
        }
        S[j][j] = std::sqrt(sum);
        const float inv_diag = 1.0f / S[j][j];
        for (int i = j + 1; i < n; ++i) {
            float s2 = S[i][j];
            for (int k = 0; k < j; ++k) s2 -= S[i][k] * S[j][k];
            S[i][j] = s2 * inv_diag;
        }
    }

    // 7) Solve K = PHt · S⁻¹  ⟺  S · Kᵀ = PHtᵀ  (S is symmetric so
    //    Sᵀ = S in Python's np.linalg.solve(S.T, PHt.T)). One forward-
    //    back substitution per row of PHt → 11 systems, each O(n²).
    float K_mat[N_STATES][N_MEAS_MAX];
    for (int i = 0; i < N_STATES; ++i) {
        float vec[N_MEAS_MAX];
        // Forward: L · vec = PHt[i,:]ᵀ
        for (int a = 0; a < n; ++a) {
            float sum = PHt[i][a];
            for (int b = 0; b < a; ++b) sum -= S[a][b] * vec[b];
            vec[a] = sum / S[a][a];
        }
        // Back: Lᵀ · K[i,:]ᵀ = vec
        for (int a = n - 1; a >= 0; --a) {
            float sum = vec[a];
            for (int b = a + 1; b < n; ++b) sum -= S[b][a] * K_mat[i][b];
            K_mat[i][a] = sum / S[a][a];
        }
    }

    // 8) x = x + K · y.
    for (int i = 0; i < N_STATES; ++i) {
        float dx = 0.0f;
        for (int j = 0; j < n; ++j) dx += K_mat[i][j] * y[j];
        x_[i] += dx;
    }

    // 9) Joseph-form covariance update:
    //       P = (I − K·H) · P · (I − K·H)ᵀ + K · R · Kᵀ
    //    This is the numerically-stable form Python uses. Algebraically
    //    it equals the simple (I − K·H)·P update; fp32 round-off can
    //    push the simple form non-PSD, but Joseph stays PSD.
    //
    // 9a) I_KH = I − K·H  (stored in KH).
    float KH[N_STATES][N_STATES];
    for (int i = 0; i < N_STATES; ++i) {
        for (int j = 0; j < N_STATES; ++j) {
            float s = 0.0f;
            for (int k = 0; k < n; ++k) s += K_mat[i][k] * H[k][j];
            KH[i][j] = ((i == j) ? 1.0f : 0.0f) - s;
        }
    }
    // 9b) A = (I − K·H) · P  (full 11×11 matmul).
    float A[N_STATES][N_STATES];
    for (int i = 0; i < N_STATES; ++i) {
        for (int j = 0; j < N_STATES; ++j) {
            float s = 0.0f;
            for (int k = 0; k < N_STATES; ++k) s += KH[i][k] * P_[k][j];
            A[i][j] = s;
        }
    }
    // 9c) P_new = A · (I − K·H)ᵀ  +  K · R · Kᵀ.
    //     R is diagonal → (K · R · Kᵀ)[i][j] = Σ_m K[i][m] · R_diag[m] · K[j][m].
    for (int i = 0; i < N_STATES; ++i) {
        for (int j = 0; j < N_STATES; ++j) {
            float s = 0.0f;
            // A · (I − K·H)ᵀ — note (I-KH)ᵀ[k][j] = KH[j][k]
            for (int k = 0; k < N_STATES; ++k) s += A[i][k] * KH[j][k];
            // K · R · Kᵀ contribution.
            for (int m = 0; m < n; ++m) s += K_mat[i][m] * R_diag[m] * K_mat[j][m];
            P_[i][j] = s;
        }
    }

    // 10) Quaternion normalisation (same fp32 hygiene Python does).
    renormaliseQuaternion();
}

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------

EKFQ::State EKFQ::getState() const {
    State s;
    s.q0 = x_[Q0]; s.q1 = x_[Q1]; s.q2 = x_[Q2]; s.q3 = x_[Q3];
    s.bp = x_[BP_IDX]; s.bq = x_[BQ_IDX]; s.br = x_[BR_IDX];
    s.z  = x_[Z];  s.vz = x_[VZ]; s.b_az = x_[B_AZ];
    s.beta = x_[BETA];
    return s;
}

float EKFQ::alphaKinematicRad(float tas_mps) const {
    // Universal kinematic AOA formula with β correction. Matches the
    // Python reference's alpha_kinematic() exactly so the firmware AOA
    // output is bit-equivalent to the Optuna-tuned filter's outputs.
    const State s = getState();
    const float phi   = s.roll_rad();
    const float theta = s.pitch_rad();
    if (tas_mps < 0.5f) {
        // No reliable air-relative velocity — return geometric level-AOA.
        const float den = std::cos(phi) * std::cos(theta);
        return std::atan2(std::sin(theta), den > 1e-6f ? den : 1e-6f);
    }
    const float sth = std::sin(theta), cth = std::cos(theta);
    const float sph = std::sin(phi),   cph = std::cos(phi);
    const float sb  = std::sin(s.beta), cb  = std::cos(s.beta);
    const float base = std::atan2(sth, cph * cth);
    const float denom_sq = sth * sth + cph * cph * cth * cth;
    float denom = cb * std::sqrt(denom_sq);
    if (denom < 1e-6f) denom = 1e-6f;
    float arg = (s.vz / tas_mps - sph * cth * sb) / denom;
    if (arg >  1.0f) arg =  1.0f;
    if (arg < -1.0f) arg = -1.0f;
    return base + std::asin(arg);
}

}  // namespace onspeed
