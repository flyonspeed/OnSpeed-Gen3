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
//   • Correct: 8 sequential scalar updates. The 5 single-non-zero H
//     rows (baro, β prior, 3 bias priors) use the scalarUpdateUnit
//     helper that collapses the PHt inner-product loop to a column-of-P
//     read. The 3 accel rows have 4–5 non-zeros and use specialised
//     scalarUpdate{Quat4,Quat4Plus1} helpers.
//
//   • All memory is stack — no heap, no allocators, no exceptions.
//
//   • Covariance symmetry is preserved by writing upper triangle then
//     mirroring (cheaper than full P = 0.5·(P + Pᵀ)).

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
    P_[BP][BP] = config_.p_bias;
    P_[BQ][BQ] = config_.p_bias;
    P_[BR][BR] = config_.p_bias;
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

// ---------------------------------------------------------------------------
// Scalar-update specialisations (anonymous namespace; static-inline so the
// compiler can fold the unrolling into the call site).
// ---------------------------------------------------------------------------

namespace {

constexpr int N = EKFQ::N_STATES;

/// H = e_idx (single 1.0f at column `idx`). PHt collapses to P's column.
///
/// Aliasing note for the in-place rank-1 downdate below: PHt[j] is read
/// as `P[j][idx]` while we write `P[i][j]` with i ≤ j (upper triangle).
/// `P[j][idx]` is only mutated at outer-iteration `i == j` (writing
/// `P[i][j]` for the special case j == idx, or when the symmetric
/// mirror pass below copies the upper triangle down). Both writes
/// happen AFTER the read at outer-iteration `i = j` finishes — so
/// every read of `P[j][idx]` precedes its own write. No aliasing hazard.
inline void scalarUpdateUnit(float P[N][N], float x[N],
                             int idx, float innovation, float R_var) {
    const float S = P[idx][idx] + R_var;
    if (S <= 0.0f) return;
    const float invS = 1.0f / S;
    float K[N];
    for (int i = 0; i < N; ++i) K[i] = P[i][idx] * invS;
    for (int i = 0; i < N; ++i) x[i] += K[i] * innovation;
    // P -= K · PHtᵀ;  PHt[j] = P[j][idx]. Upper-triangle then mirror.
    for (int i = 0; i < N; ++i) {
        const float Ki = K[i];
        for (int j = i; j < N; ++j) {
            P[i][j] -= Ki * P[j][idx];
        }
    }
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            P[j][i] = P[i][j];
        }
    }
}

/// H has FOUR non-zero entries — used by the accel-x measurement
/// (quaternion-block partials only, no centripetal bias coupling).
inline void scalarUpdateQuat4(float P[N][N], float x[N],
                              float c0, float c1, float c2, float c3,
                              float innovation, float R_var) {
    // PHt[i] = c0·P[i][Q0] + c1·P[i][Q1] + c2·P[i][Q2] + c3·P[i][Q3]
    float PHt[N];
    for (int i = 0; i < N; ++i) {
        PHt[i] = c0 * P[i][EKFQ::Q0] + c1 * P[i][EKFQ::Q1]
               + c2 * P[i][EKFQ::Q2] + c3 * P[i][EKFQ::Q3];
    }
    const float S = c0 * PHt[EKFQ::Q0] + c1 * PHt[EKFQ::Q1]
                  + c2 * PHt[EKFQ::Q2] + c3 * PHt[EKFQ::Q3] + R_var;
    if (S <= 0.0f) return;
    const float invS = 1.0f / S;
    float K[N];
    for (int i = 0; i < N; ++i) K[i] = PHt[i] * invS;
    for (int i = 0; i < N; ++i) x[i] += K[i] * innovation;
    for (int i = 0; i < N; ++i) {
        const float Ki = K[i];
        for (int j = i; j < N; ++j) {
            P[i][j] -= Ki * PHt[j];
        }
    }
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            P[j][i] = P[i][j];
        }
    }
}

/// H has 4 quaternion entries plus ONE extra non-zero at `bias_idx` — used
/// by accel-y (BR coupling) and accel-z (BQ coupling).
inline void scalarUpdateQuat4Plus1(float P[N][N], float x[N],
                                   float c0, float c1, float c2, float c3,
                                   int bias_idx, float c_bias,
                                   float innovation, float R_var) {
    float PHt[N];
    for (int i = 0; i < N; ++i) {
        PHt[i] = c0 * P[i][EKFQ::Q0] + c1 * P[i][EKFQ::Q1]
               + c2 * P[i][EKFQ::Q2] + c3 * P[i][EKFQ::Q3]
               + c_bias * P[i][bias_idx];
    }
    const float S = c0 * PHt[EKFQ::Q0] + c1 * PHt[EKFQ::Q1]
                  + c2 * PHt[EKFQ::Q2] + c3 * PHt[EKFQ::Q3]
                  + c_bias * PHt[bias_idx] + R_var;
    if (S <= 0.0f) return;
    const float invS = 1.0f / S;
    float K[N];
    for (int i = 0; i < N; ++i) K[i] = PHt[i] * invS;
    for (int i = 0; i < N; ++i) x[i] += K[i] * innovation;
    for (int i = 0; i < N; ++i) {
        const float Ki = K[i];
        for (int j = i; j < N; ++j) {
            P[i][j] -= Ki * PHt[j];
        }
    }
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            P[j][i] = P[i][j];
        }
    }
}

}  // namespace

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
    const float bp = x_[BP], bq = x_[BQ], br = x_[BR];
    const float vz = x_[VZ], b_az = x_[B_AZ], beta = x_[BETA];

    // Bias-corrected gyros.
    const float p_c = p   - bp;
    const float q_c = q_in - bq;
    const float r_c = r_in - br;

    // 1) Quaternion derivative q̇ = 0.5 · Ω(ω) · q.
    const float half_dt = 0.5f * dt;
    const float q0_dot = half_dt * (-q1 * p_c - q2 * q_c - q3 * r_c);
    const float q1_dot = half_dt * ( q0 * p_c + q2 * r_c - q3 * q_c);
    const float q2_dot = half_dt * ( q0 * q_c - q1 * r_c + q3 * p_c);
    const float q3_dot = half_dt * ( q0 * r_c + q1 * q_c - q2 * p_c);

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
    // Rows that are PURE IDENTITY: BP, BQ, BR, B_AZ (and BETA when !tas_active).
    // All other rows have a small fixed pattern of δF entries:
    //
    //   Q0 row: 7 δF entries  (Q1, Q2, Q3 quaternion-block + BP, BQ, BR bias)
    //   Q1 row: 7
    //   Q2 row: 7
    //   Q3 row: 7
    //   Z  row: 6  (Q0..Q3 quat + VZ + B_AZ)
    //   VZ row: 5  (Q0..Q3 quat + B_AZ)
    //   BETA row: 5  (Q0..Q3 quat + BR)   — only when tas_active

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
    //    where F deviates from identity. Identity rows of F (BP, BQ, BR,
    //    B_AZ, and possibly BETA) trivially give M[i][j] = P[i][j].

    float M_Q0[N], M_Q1[N], M_Q2[N], M_Q3[N];
    float M_Z[N], M_VZ[N], M_BETA[N];
    for (int j = 0; j < N; ++j) {
        const float Pq0 = P_[Q0][j], Pq1 = P_[Q1][j];
        const float Pq2 = P_[Q2][j], Pq3 = P_[Q3][j];
        const float Pbp = P_[BP][j], Pbq = P_[BQ][j], Pbr = P_[BR][j];
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

    for (int i = 0; i < N; ++i) {
        // Cache M[i][:] into locals. Source is one of M_* buffers (for
        // non-identity rows) or P_[i][:] (for identity rows).
        float Mi_Q0, Mi_Q1, Mi_Q2, Mi_Q3;
        float Mi_BP, Mi_BQ, Mi_BR;
        float Mi_Z,  Mi_VZ, Mi_BAZ;
        float Mi_BETA;
        switch (i) {
            case Q0:
                Mi_Q0 = M_Q0[Q0]; Mi_Q1 = M_Q0[Q1]; Mi_Q2 = M_Q0[Q2]; Mi_Q3 = M_Q0[Q3];
                Mi_BP = M_Q0[BP]; Mi_BQ = M_Q0[BQ]; Mi_BR = M_Q0[BR];
                Mi_Z  = M_Q0[Z];  Mi_VZ = M_Q0[VZ]; Mi_BAZ = M_Q0[B_AZ];
                Mi_BETA = M_Q0[BETA];
                break;
            case Q1:
                Mi_Q0 = M_Q1[Q0]; Mi_Q1 = M_Q1[Q1]; Mi_Q2 = M_Q1[Q2]; Mi_Q3 = M_Q1[Q3];
                Mi_BP = M_Q1[BP]; Mi_BQ = M_Q1[BQ]; Mi_BR = M_Q1[BR];
                Mi_Z  = M_Q1[Z];  Mi_VZ = M_Q1[VZ]; Mi_BAZ = M_Q1[B_AZ];
                Mi_BETA = M_Q1[BETA];
                break;
            case Q2:
                Mi_Q0 = M_Q2[Q0]; Mi_Q1 = M_Q2[Q1]; Mi_Q2 = M_Q2[Q2]; Mi_Q3 = M_Q2[Q3];
                Mi_BP = M_Q2[BP]; Mi_BQ = M_Q2[BQ]; Mi_BR = M_Q2[BR];
                Mi_Z  = M_Q2[Z];  Mi_VZ = M_Q2[VZ]; Mi_BAZ = M_Q2[B_AZ];
                Mi_BETA = M_Q2[BETA];
                break;
            case Q3:
                Mi_Q0 = M_Q3[Q0]; Mi_Q1 = M_Q3[Q1]; Mi_Q2 = M_Q3[Q2]; Mi_Q3 = M_Q3[Q3];
                Mi_BP = M_Q3[BP]; Mi_BQ = M_Q3[BQ]; Mi_BR = M_Q3[BR];
                Mi_Z  = M_Q3[Z];  Mi_VZ = M_Q3[VZ]; Mi_BAZ = M_Q3[B_AZ];
                Mi_BETA = M_Q3[BETA];
                break;
            case Z:
                Mi_Q0 = M_Z[Q0]; Mi_Q1 = M_Z[Q1]; Mi_Q2 = M_Z[Q2]; Mi_Q3 = M_Z[Q3];
                Mi_BP = M_Z[BP]; Mi_BQ = M_Z[BQ]; Mi_BR = M_Z[BR];
                Mi_Z  = M_Z[Z];  Mi_VZ = M_Z[VZ]; Mi_BAZ = M_Z[B_AZ];
                Mi_BETA = M_Z[BETA];
                break;
            case VZ:
                Mi_Q0 = M_VZ[Q0]; Mi_Q1 = M_VZ[Q1]; Mi_Q2 = M_VZ[Q2]; Mi_Q3 = M_VZ[Q3];
                Mi_BP = M_VZ[BP]; Mi_BQ = M_VZ[BQ]; Mi_BR = M_VZ[BR];
                Mi_Z  = M_VZ[Z];  Mi_VZ = M_VZ[VZ]; Mi_BAZ = M_VZ[B_AZ];
                Mi_BETA = M_VZ[BETA];
                break;
            case BETA:
                Mi_Q0 = M_BETA[Q0]; Mi_Q1 = M_BETA[Q1]; Mi_Q2 = M_BETA[Q2]; Mi_Q3 = M_BETA[Q3];
                Mi_BP = M_BETA[BP]; Mi_BQ = M_BETA[BQ]; Mi_BR = M_BETA[BR];
                Mi_Z  = M_BETA[Z];  Mi_VZ = M_BETA[VZ]; Mi_BAZ = M_BETA[B_AZ];
                Mi_BETA = M_BETA[BETA];
                break;
            default:  // BP, BQ, BR, B_AZ — identity rows of F
                Mi_Q0 = P_[i][Q0]; Mi_Q1 = P_[i][Q1]; Mi_Q2 = P_[i][Q2]; Mi_Q3 = P_[i][Q3];
                Mi_BP = P_[i][BP]; Mi_BQ = P_[i][BQ]; Mi_BR = P_[i][BR];
                Mi_Z  = P_[i][Z];  Mi_VZ = P_[i][VZ]; Mi_BAZ = P_[i][B_AZ];
                Mi_BETA = P_[i][BETA];
                break;
        }

        // Now write the 11 columns of P_new[i][:] = M[i][:] · F^T.
        // Identity columns (BP/BQ/BR/B_AZ): just write the cached M value.
        P_[i][BP]   = Mi_BP;
        P_[i][BQ]   = Mi_BQ;
        P_[i][BR]   = Mi_BR;
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
    P_[BP][BP] += config_.q_bias * dt;
    P_[BQ][BQ] += config_.q_bias * dt;
    P_[BR][BR] += config_.q_bias * dt;
    P_[Z][Z]   += config_.q_z    * dt;
    P_[VZ][VZ] += config_.q_vz   * dt;
    P_[B_AZ][B_AZ] += config_.q_b_az * dt;
    P_[BETA][BETA] += config_.q_beta * dt;

    // 9) Commit state.
    x_[Q0] = q0 + q0_dot;
    x_[Q1] = q1 + q1_dot;
    x_[Q2] = q2 + q2_dot;
    x_[Q3] = q3 + q3_dot;
    x_[Z]    = new_z;
    x_[VZ]   = new_vz;
    x_[BETA] = new_beta;
    renormaliseQuaternion();
}

// ---------------------------------------------------------------------------
// Correct step — sequential scalar updates with sparse H specialisations
// ---------------------------------------------------------------------------

void EKFQ::correct(float ax_meas, float ay_meas, float az_meas,
                   float tas, float tasDot,
                   float pitchRate, float yawRate,
                   float baroZ, bool updateBaro) {
    const float q0 = x_[Q0], q1 = x_[Q1], q2 = x_[Q2], q3 = x_[Q3];
    const float bq = x_[BQ], br = x_[BR];
    const float z = x_[Z], beta = x_[BETA];
    const float g = GRAVITY;

    // Bias-corrected gyros for h(x) centripetal terms.
    const float q_c = pitchRate - bq;
    const float r_c = yawRate   - br;

    // Predicted body specific force = gravity-only + centripetal/TASdot.
    const float ax_pred = -2.0f * g * (q1 * q3 - q0 * q2) + tasDot;
    const float ay_pred = -2.0f * g * (q2 * q3 + q0 * q1) + tas * r_c;
    const float az_pred =       -g * (q0*q0 - q1*q1 - q2*q2 + q3*q3) - tas * q_c;

    // ----- accel x: H has 4 quat non-zeros, no bias coupling -----
    scalarUpdateQuat4(P_, x_,
                      /* c0 */  2.0f * g * q2,
                      /* c1 */ -2.0f * g * q3,
                      /* c2 */  2.0f * g * q0,
                      /* c3 */ -2.0f * g * q1,
                      ax_meas - ax_pred, config_.r_ax);

    // ----- accel y: 4 quat + BR (β-adaptive R) -----
    {
        const float Ray = config_.r_ay
                          * (1.0f + config_.k_beta_R * beta * beta);
        scalarUpdateQuat4Plus1(P_, x_,
                               /* c0 */ -2.0f * g * q1,
                               /* c1 */ -2.0f * g * q0,
                               /* c2 */ -2.0f * g * q3,
                               /* c3 */ -2.0f * g * q2,
                               /* bias_idx */ BR,
                               /* c_bias */ -tas,
                               ay_meas - ay_pred, Ray);
    }

    // ----- accel z: 4 quat + BQ -----
    scalarUpdateQuat4Plus1(P_, x_,
                           /* c0 */ -2.0f * g * q0,
                           /* c1 */  2.0f * g * q1,
                           /* c2 */  2.0f * g * q2,
                           /* c3 */ -2.0f * g * q3,
                           /* bias_idx */ BQ,
                           /* c_bias */ +tas,
                           az_meas - az_pred, config_.r_az);

    // ----- Baro altitude (only when fresh / valid) -----
    if (updateBaro) {
        scalarUpdateUnit(P_, x_, Z, baroZ - z, config_.r_baro);
    }
    // ----- β = 0 weak prior -----
    scalarUpdateUnit(P_, x_, BETA, 0.0f - x_[BETA], config_.r_beta_prior);
    // ----- Gyro-bias zero-mean priors -----
    scalarUpdateUnit(P_, x_, BP, 0.0f - x_[BP], config_.r_bias_prior);
    scalarUpdateUnit(P_, x_, BQ, 0.0f - x_[BQ], config_.r_bias_prior);
    scalarUpdateUnit(P_, x_, BR, 0.0f - x_[BR], config_.r_bias_prior);

    renormaliseQuaternion();
}

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------

EKFQ::State EKFQ::getState() const {
    State s;
    s.q0 = x_[Q0]; s.q1 = x_[Q1]; s.q2 = x_[Q2]; s.q3 = x_[Q3];
    s.bp = x_[BP]; s.bq = x_[BQ]; s.br = x_[BR];
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
