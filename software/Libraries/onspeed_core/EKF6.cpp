/**
 * @file EKF6.cpp
 * @brief Implementation of 6-State Extended Kalman Filter
 *
 * See EKF6.h for detailed algorithm documentation.
 *
 * @section algorithm_notes Implementation Notes
 *
 * 1. Matrix Operations:
 *    - All matrices stored as float arrays for embedded efficiency
 *    - Covariance matrix P is stored as full 6x6 (not exploiting symmetry)
 *    - Q and R are diagonal, stored as 1D arrays
 *
 * 2. Numerical Stability:
 *    - Singularity protection at theta = ±90 degrees
 *    - Joseph form covariance update for numerical stability
 *    - Gauss-Jordan with partial pivoting for matrix inversion
 *
 * 3. Comparison with Octave Reference:
 *    - C++ and Octave implementations should match within 0.001 degrees
 *    - Any discrepancy larger than this indicates a bug
 *
 * @section known_limitations Known Limitations
 *
 * 1. Gimbal Lock: Filter becomes singular at theta = ±90 degrees.
 *    The SINGULARITY_THRESHOLD limits tan(theta) but doesn't solve
 *    the fundamental Euler angle limitation. For aerobatic flight,
 *    consider quaternion-based EKF.
 *
 * 2. Accelerometer Convention: The measurement model expects the
 *    aerospace sign convention where az = -g in level flight (sensor
 *    measures reaction to gravity). The caller (AHRS.cpp) must negate
 *    the vertical axis if the IMU pipeline uses NED convention where
 *    az = +g in level flight. Non-gravitational accelerations (TASdot,
 *    centripetal) are removed upstream in AHRS.cpp before the EKF sees
 *    the data, so the gravity-only model is correct for the compensated
 *    inputs it receives.
 *
 * 3. Alpha Observability: AOA is only observable when gamma is known.
 *    If gamma=0 always, alpha will track theta exactly (which may
 *    or may not be correct depending on flight conditions).
 */

#include "EKF6.h"
#include <cmath>
#include <cstring>

namespace onspeed {

//==============================================================================
// Constants
//==============================================================================

/// Pi constant for degree/radian conversion
static constexpr float PI = 3.14159265358979f;

/// Radians to degrees conversion factor
static constexpr float RAD2DEG = 180.0f / PI;

//==============================================================================
// State Convenience Methods
//==============================================================================

float EKF6::State::phi_deg() const { return phi * RAD2DEG; }
float EKF6::State::theta_deg() const { return theta * RAD2DEG; }
float EKF6::State::alpha_deg() const { return alpha * RAD2DEG; }
float EKF6::State::bp_dps() const { return bp * RAD2DEG; }
float EKF6::State::bq_dps() const { return bq * RAD2DEG; }
float EKF6::State::br_dps() const { return br * RAD2DEG; }

//==============================================================================
// Constructors
//==============================================================================

EKF6::EKF6() : EKF6(Config::defaults()) {}

EKF6::EKF6(const Config& cfg) : config_(cfg), initialized_(false) {
    // Initialize process noise diagonal (Q matrix)
    // Q = diag([q_att, q_att, q_alpha, q_bias, q_bias, q_bias])
    Q_[0] = cfg.q_attitude;  // phi
    Q_[1] = cfg.q_attitude;  // theta
    Q_[2] = cfg.q_alpha;     // alpha
    Q_[3] = cfg.q_bias;      // bp
    Q_[4] = cfg.q_bias;      // bq
    Q_[5] = cfg.q_bias;      // br

    // Initialize measurement noise diagonal (R matrix)
    // R = diag([r_accel, r_accel, r_accel, r_alpha])
    R_[0] = cfg.r_accel;  // ax
    R_[1] = cfg.r_accel;  // ay
    R_[2] = cfg.r_accel;  // az
    R_[3] = cfg.r_alpha;  // alpha_meas

    // Initialize state to zero
    init();
}

//==============================================================================
// Initialization
//==============================================================================

void EKF6::init(float initial_phi, float initial_theta) {
    // Initialize state vector
    x_[0] = initial_phi;    // phi (roll)
    x_[1] = initial_theta;  // theta (pitch)
    x_[2] = 0.0f;           // alpha (AOA) - will be estimated
    x_[3] = 0.0f;           // bp (roll gyro bias)
    x_[4] = 0.0f;           // bq (pitch gyro bias)
    x_[5] = 0.0f;           // br (yaw gyro bias)

    // Initialize covariance matrix P = diag([p_att, p_att, p_alpha, p_bias, ...])
    std::memset(P_, 0, sizeof(P_));
    P_[0][0] = config_.p_attitude;  // phi uncertainty
    P_[1][1] = config_.p_attitude;  // theta uncertainty
    P_[2][2] = config_.p_alpha;     // alpha uncertainty
    P_[3][3] = config_.p_bias;      // bp uncertainty
    P_[4][4] = config_.p_bias;      // bq uncertainty
    P_[5][5] = config_.p_bias;      // br uncertainty

    initialized_ = true;
}

//==============================================================================
// Main Update Loop
//==============================================================================

void EKF6::update(const Measurements& meas, float dt) {
    if (!initialized_) {
        init();
    }

    // Step 1: Predict (time update) using gyroscope measurements
    predict(meas.p, meas.q, meas.r, dt);

    // Step 2: Correct (measurement update) using accelerometer
    correct(meas.ax, meas.ay, meas.az, meas.gamma, GRAVITY);
}

EKF6::State EKF6::getState() const {
    return {x_[0], x_[1], x_[2], x_[3], x_[4], x_[5]};
}

void EKF6::resetAlphaCovariance() {
    if (!initialized_) return;
    for (int i = 0; i < N_STATES; i++) {
        P_[2][i] = 0.0f;
        P_[i][2] = 0.0f;
    }
    P_[2][2] = config_.p_alpha;
}

//==============================================================================
// Prediction Step
//==============================================================================

void EKF6::predict(float p, float q, float r, float dt) {
    /*
     * State prediction using Euler angle kinematics:
     *
     *   phi_dot   = p_c + q_c*sin(phi)*tan(theta) + r_c*cos(phi)*tan(theta)
     *   theta_dot = q_c*cos(phi) - r_c*sin(phi)
     *   alpha_dot = 0
     *   bias_dot  = 0
     *
     * where p_c = p - bp, q_c = q - bq, r_c = r - br are bias-corrected rates.
     *
     * Note: This is a first-order Euler integration. For more accuracy at
     * lower sample rates, consider RK4 integration.
     */

    // Extract current state estimates
    const float phi = x_[0];
    const float theta = x_[1];
    const float bp = x_[3];
    const float bq = x_[4];
    const float br = x_[5];

    // Compute bias-corrected angular rates
    const float p_corr = p - bp;
    const float q_corr = q - bq;
    const float r_corr = r - br;

    // Compute trigonometric terms
    const float sph = std::sin(phi);
    const float cph = std::cos(phi);
    float cth = std::cos(theta);

    // Singularity protection: prevent division by zero at theta = ±90°
    // This clamps tan(theta) to approximately ±1000
    if (std::fabs(cth) < SINGULARITY_THRESHOLD) {
        cth = (cth >= 0.0f) ? SINGULARITY_THRESHOLD : -SINGULARITY_THRESHOLD;
    }

    const float sth = std::sin(theta);
    const float tth = sth / cth;  // tan(theta)

    // Compute state derivatives
    const float phi_dot = p_corr + q_corr * sph * tth + r_corr * cph * tth;
    const float theta_dot = q_corr * cph - r_corr * sph;

    // Integrate state (Euler method)
    x_[0] = phi + dt * phi_dot;    // phi
    x_[1] = theta + dt * theta_dot; // theta
    // x_[2] (alpha) unchanged - modeled as constant
    // x_[3:5] (biases) unchanged - modeled as random walk via Q

    /*
     * State transition Jacobian F = I + dt * A
     *
     * F has identity rows 2-5 (alpha and gyro biases are modeled as
     * constant in the kinematic equations, so their state-transition
     * rows are pure identity). Only the phi (row 0) and theta (row 1)
     * dynamics produce non-trivial Jacobian entries:
     *
     *   F = [ F00  F01   0   F03  F04  F05 ]
     *       [ F10   1    0    0   F14  F15 ]
     *       [  0    0    1    0    0    0  ]
     *       [  0    0    0    1    0    0  ]
     *       [  0    0    0    0    1    0  ]
     *       [  0    0    0    0    0    1  ]
     *
     * Stored as scalar locals instead of a 6x6 array (saves 144 bytes
     * stack + memset).
     */

    // Row 0: phi derivatives
    const float F00 = 1.0f + dt * (q_corr * cph * tth - r_corr * sph * tth);
    const float sec2th = 1.0f + tth * tth;  // sec²(theta) = 1 + tan²(theta)
    const float F01 = dt * (q_corr * sph * sec2th + r_corr * cph * sec2th);
    const float F03 = -dt;                 // d(phi_dot)/d(bp)
    const float F04 = -dt * sph * tth;     // d(phi_dot)/d(bq)
    const float F05 = -dt * cph * tth;     // d(phi_dot)/d(br)

    // Row 1: theta derivatives (F[1][1] = 1 implicit)
    const float F10 = dt * (-q_corr * sph - r_corr * cph);
    const float F14 = -dt * cph;           // d(theta_dot)/d(bq)
    const float F15 = dt * sph;            // d(theta_dot)/d(br)

    /*
     * Sparse FP = F * P
     *
     * Since F rows 2-5 are identity, FP[2..5] = P[2..5] (just copy).
     * Only rows 0-1 need actual dot products.
     */

    float FP[N_STATES][N_STATES];

    // Row 0: FP[0][j] = F00*P[0][j] + F01*P[1][j] + F03*P[3][j] + F04*P[4][j] + F05*P[5][j]
    for (int j = 0; j < N_STATES; j++) {
        FP[0][j] = F00 * P_[0][j] + F01 * P_[1][j]
                 + F03 * P_[3][j] + F04 * P_[4][j] + F05 * P_[5][j];
    }

    // Row 1: FP[1][j] = F10*P[0][j] + P[1][j] + F14*P[4][j] + F15*P[5][j]
    for (int j = 0; j < N_STATES; j++) {
        FP[1][j] = F10 * P_[0][j] + P_[1][j]
                 + F14 * P_[4][j] + F15 * P_[5][j];
    }

    // Rows 2-5: identity rows just copy P
    std::memcpy(FP[2], P_[2], 4 * N_STATES * sizeof(float));

    /*
     * Sparse P_new = FP * F' + Q
     *
     * F' columns 2-5 are identity (since F rows 2-5 are identity),
     * so P_new[i][j] = FP[i][j] for j >= 2. Only columns 0-1 need
     * dot products:
     *
     *   P_new[i][0] = FP[i][0]*F00 + FP[i][1]*F10
     *               + FP[i][3]*F03 + FP[i][4]*F04 + FP[i][5]*F05
     *   P_new[i][1] = FP[i][0]*F01 + FP[i][1]
     *               + FP[i][4]*F14 + FP[i][5]*F15
     *   P_new[i][2..5] = FP[i][2..5]
     */

    float P_new[N_STATES][N_STATES];
    for (int i = 0; i < N_STATES; i++) {
        // Column 0: P_new[i][0] = sum_k FP[i][k]*F'[k][0] = sum_k FP[i][k]*F[0][k]
        P_new[i][0] = FP[i][0] * F00 + FP[i][1] * F01
                    + FP[i][3] * F03 + FP[i][4] * F04 + FP[i][5] * F05;

        // Column 1: P_new[i][1] = sum_k FP[i][k]*F[1][k]
        P_new[i][1] = FP[i][0] * F10 + FP[i][1]
                    + FP[i][4] * F14 + FP[i][5] * F15;

        // Columns 2-5: identity columns of F'
        P_new[i][2] = FP[i][2];
        P_new[i][3] = FP[i][3];
        P_new[i][4] = FP[i][4];
        P_new[i][5] = FP[i][5];
    }

    // Add process noise (diagonal only)
    for (int i = 0; i < N_STATES; i++) {
        P_new[i][i] += Q_[i];
    }

    // Copy result back to P_
    std::memcpy(P_, P_new, sizeof(P_));
}

//==============================================================================
// Correction Step
//==============================================================================

void EKF6::correct(float ax, float ay, float az, float gamma, float g) {
    /*
     * Measurement model:
     *
     * The accelerometer measures gravity in the body frame. Assuming
     * negligible aircraft acceleration (valid in 1G flight):
     *
     *   ax_pred =  g * sin(theta)
     *   ay_pred = -g * cos(theta) * sin(phi)
     *   az_pred = -g * cos(theta) * cos(phi)
     *
     * The alpha measurement comes from the kinematic relationship:
     *
     *   alpha = theta - gamma
     *
     * where gamma is the flight path angle (from VSI/TAS).
     *
     * IMPORTANT: The accelerometer sign convention assumes:
     *   - In level flight: ax=0, ay=0, az=-g
     *   - This is NED body frame where Z points down
     *   - The sensor reports the reaction to gravity (not gravity itself)
     */

    // Extract predicted state
    const float phi = x_[0];
    const float theta = x_[1];
    const float alpha = x_[2];

    // Trigonometric terms
    const float sph = std::sin(phi);
    const float cph = std::cos(phi);
    const float sth = std::sin(theta);
    const float cth = std::cos(theta);

    // Predicted measurements
    float z_pred[N_MEAS];
    z_pred[0] = g * sth;            // ax predicted
    z_pred[1] = -g * cth * sph;     // ay predicted
    z_pred[2] = -g * cth * cph;     // az predicted
    z_pred[3] = alpha;              // alpha predicted (identity)

    /*
     * Measurement Jacobian H = dh/dx (sparse scalars)
     *
     * H is 4x6 with only 6 non-zero entries. Stored as scalar locals
     * instead of a 4x6 array (saves 96 bytes stack + memset):
     *
     *   H = [  0   H01   0   0   0   0 ]
     *       [ H10  H11   0   0   0   0 ]
     *       [ H20  H21   0   0   0   0 ]
     *       [  0    0    1   0   0   0 ]
     *
     * H[3][2] = 1 is implicit (identity for alpha measurement).
     */

    const float H01 = g * cth;              // d(ax)/d(theta)
    const float H10 = -g * cth * cph;       // d(ay)/d(phi)
    const float H11 = g * sth * sph;        // d(ay)/d(theta)
    const float H20 = g * cth * sph;        // d(az)/d(phi)
    const float H21 = g * sth * cph;        // d(az)/d(theta)
    // H32 = 1.0f implicit

    // Actual measurements
    const float alpha_meas = theta - gamma;
    float z[N_MEAS] = {ax, ay, az, alpha_meas};

    // Innovation (measurement residual): y = z - z_pred
    float y[N_MEAS];
    for (int i = 0; i < N_MEAS; i++) {
        y[i] = z[i] - z_pred[i];
    }

    /*
     * Sparse HP = H * P
     *
     * Each row of H has at most 2 non-zero entries (plus H32=1):
     *   HP[0][j] = H01 * P[1][j]
     *   HP[1][j] = H10 * P[0][j] + H11 * P[1][j]
     *   HP[2][j] = H20 * P[0][j] + H21 * P[1][j]
     *   HP[3][j] = P[2][j]  (H32=1, only col 2 non-zero)
     */

    float HP[N_MEAS][N_STATES];
    for (int j = 0; j < N_STATES; j++) {
        HP[0][j] = H01 * P_[1][j];
        HP[1][j] = H10 * P_[0][j] + H11 * P_[1][j];
        HP[2][j] = H20 * P_[0][j] + H21 * P_[1][j];
        HP[3][j] = P_[2][j];
    }

    /*
     * Sparse S = HP * H' + R
     *
     * H' has non-zero entries only in rows 0,1,2 (columns of H that
     * are non-zero). H'[k][m] = H[m][k], so:
     *   S[i][0] = HP[i][1] * H01
     *   S[i][1] = HP[i][0] * H10 + HP[i][1] * H11
     *   S[i][2] = HP[i][0] * H20 + HP[i][1] * H21
     *   S[i][3] = HP[i][2]  (H32=1)
     */

    float S[N_MEAS][N_MEAS];
    for (int i = 0; i < N_MEAS; i++) {
        S[i][0] = HP[i][1] * H01;
        S[i][1] = HP[i][0] * H10 + HP[i][1] * H11;
        S[i][2] = HP[i][0] * H20 + HP[i][1] * H21;
        S[i][3] = HP[i][2];
    }

    // Add measurement noise (diagonal)
    for (int i = 0; i < N_MEAS; i++) {
        S[i][i] += R_[i];
    }

    // Invert S using Gauss-Jordan elimination
    float S_inv[N_MEAS][N_MEAS];
    if (!invert4x4(S, S_inv)) {
        // Matrix is singular - skip this update
        return;
    }

    /*
     * Sparse PHt = P * H'
     *
     * H' has non-zero entries:
     *   H'[0][1] = H10,  H'[0][2] = H20
     *   H'[1][0] = H01,  H'[1][1] = H11,  H'[1][2] = H21
     *   H'[2][3] = 1
     *
     * So: PHt[i][j] = sum_k P[i][k] * H'[k][j] = sum_k P[i][k] * H[j][k]
     *   PHt[i][0] = P[i][1] * H01
     *   PHt[i][1] = P[i][0] * H10 + P[i][1] * H11
     *   PHt[i][2] = P[i][0] * H20 + P[i][1] * H21
     *   PHt[i][3] = P[i][2]
     */

    float PHt[N_STATES][N_MEAS];
    for (int i = 0; i < N_STATES; i++) {
        PHt[i][0] = P_[i][1] * H01;
        PHt[i][1] = P_[i][0] * H10 + P_[i][1] * H11;
        PHt[i][2] = P_[i][0] * H20 + P_[i][1] * H21;
        PHt[i][3] = P_[i][2];
    }

    // K = PHt * S_inv (dense — S_inv is generally full)
    float K[N_STATES][N_MEAS];
    for (int i = 0; i < N_STATES; i++) {
        for (int j = 0; j < N_MEAS; j++) {
            K[i][j] = 0.0f;
            for (int k = 0; k < N_MEAS; k++) {
                K[i][j] += PHt[i][k] * S_inv[k][j];
            }
        }
    }

    // Update state: x = x + K * y
    for (int i = 0; i < N_STATES; i++) {
        for (int j = 0; j < N_MEAS; j++) {
            x_[i] += K[i][j] * y[j];
        }
    }

    /*
     * Sparse IKH = I - K*H
     *
     * K*H: (K*H)[i][j] = sum_k K[i][k]*H[k][j]
     * H columns 3,4,5 are all zero, so:
     *   (K*H)[i][0] = K[i][1]*H10 + K[i][2]*H20
     *   (K*H)[i][1] = K[i][0]*H01 + K[i][1]*H11 + K[i][2]*H21
     *   (K*H)[i][2] = K[i][3]  (H32=1)
     *   (K*H)[i][3..5] = 0
     *
     * Therefore IKH columns 3-5 are identity columns.
     */

    float IKH[N_STATES][N_STATES];
    for (int i = 0; i < N_STATES; i++) {
        IKH[i][0] = ((i == 0) ? 1.0f : 0.0f) - K[i][1] * H10 - K[i][2] * H20;
        IKH[i][1] = ((i == 1) ? 1.0f : 0.0f) - K[i][0] * H01 - K[i][1] * H11 - K[i][2] * H21;
        IKH[i][2] = ((i == 2) ? 1.0f : 0.0f) - K[i][3];
        IKH[i][3] = (i == 3) ? 1.0f : 0.0f;
        IKH[i][4] = (i == 4) ? 1.0f : 0.0f;
        IKH[i][5] = (i == 5) ? 1.0f : 0.0f;
    }

    // IKHP = IKH * P (dense — IKH is generally full after K*H fills in)
    float IKHP[N_STATES][N_STATES];
    for (int i = 0; i < N_STATES; i++) {
        for (int j = 0; j < N_STATES; j++) {
            IKHP[i][j] = 0.0f;
            for (int k = 0; k < N_STATES; k++) {
                IKHP[i][j] += IKH[i][k] * P_[k][j];
            }
        }
    }

    // P_new = IKHP * IKH' + K * R * K' (dense — Joseph form)
    float P_new[N_STATES][N_STATES];
    for (int i = 0; i < N_STATES; i++) {
        for (int j = 0; j < N_STATES; j++) {
            P_new[i][j] = 0.0f;
            for (int k = 0; k < N_STATES; k++) {
                P_new[i][j] += IKHP[i][k] * IKH[j][k];
            }
            for (int k = 0; k < N_MEAS; k++) {
                P_new[i][j] += K[i][k] * R_[k] * K[j][k];
            }
        }
    }

    // Copy result back to P_
    std::memcpy(P_, P_new, sizeof(P_));
}

//==============================================================================
// Matrix Inversion
//==============================================================================

bool EKF6::invert4x4(const float (*A)[N_MEAS], float (*A_inv)[N_MEAS]) {
    /*
     * 4x4 matrix inversion using Gauss-Jordan elimination with partial pivoting.
     *
     * Algorithm:
     * 1. Augment [A | I] to form [A | I]
     * 2. Forward elimination to get [U | L^{-1}]
     * 3. Back substitution to get [I | A^{-1}]
     *
     * Partial pivoting (swapping rows to get largest pivot) improves
     * numerical stability.
     */

    // Create augmented matrix [A | I]
    float work[N_MEAS][N_MEAS * 2];
    for (int i = 0; i < N_MEAS; i++) {
        for (int j = 0; j < N_MEAS; j++) {
            work[i][j] = A[i][j];
            work[i][j + N_MEAS] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Gauss-Jordan elimination
    for (int col = 0; col < N_MEAS; col++) {
        // Find pivot (row with largest value in current column)
        int max_row = col;
        float max_val = std::fabs(work[col][col]);
        for (int row = col + 1; row < N_MEAS; row++) {
            float val = std::fabs(work[row][col]);
            if (val > max_val) {
                max_val = val;
                max_row = row;
            }
        }

        // Check for singularity
        if (max_val < 1e-10f) {
            return false;  // Matrix is singular or nearly singular
        }

        // Swap rows if necessary
        if (max_row != col) {
            for (int j = 0; j < N_MEAS * 2; j++) {
                float tmp = work[col][j];
                work[col][j] = work[max_row][j];
                work[max_row][j] = tmp;
            }
        }

        // Scale pivot row to make pivot = 1
        float pivot = work[col][col];
        for (int j = 0; j < N_MEAS * 2; j++) {
            work[col][j] /= pivot;
        }

        // Eliminate column in all other rows
        for (int row = 0; row < N_MEAS; row++) {
            if (row != col) {
                float factor = work[row][col];
                for (int j = 0; j < N_MEAS * 2; j++) {
                    work[row][j] -= factor * work[col][j];
                }
            }
        }
    }

    // Extract inverse from right half of augmented matrix
    for (int i = 0; i < N_MEAS; i++) {
        for (int j = 0; j < N_MEAS; j++) {
            A_inv[i][j] = work[i][j + N_MEAS];
        }
    }

    return true;
}

}  // namespace onspeed
