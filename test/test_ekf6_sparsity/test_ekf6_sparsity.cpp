/**
 * @file test_ekf6_sparsity.cpp
 * @brief Dense-vs-sparse equivalence test for EKF6 matrix optimizations
 *
 * Contains a frozen copy of the original dense predict/correct logic.
 * Runs both production EKF6 and the dense reference with identical inputs,
 * comparing state vector and P matrix after every update cycle.
 *
 * This catches any sparsity optimization bugs immediately — if a
 * zero-entry is misidentified or a term is dropped, the P matrices
 * will diverge within a few cycles.
 */

#include <unity.h>
#include <EKF6.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace onspeed;

static constexpr float DT = 1.0f / 208.0f;
static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;
static constexpr float G = 9.80665f;
static constexpr int N = EKF6::N_STATES;
static constexpr int M = EKF6::N_MEAS;

// Tolerances for dense-vs-sparse comparison
//
// The sparse code is mathematically equivalent to the dense code, but
// evaluates dot products in a different order, which changes float
// rounding. Over hundreds of cycles the P matrix entries accumulate
// ~1e-4 relative differences at float precision. These are well below
// any observable effect on the state estimates.
static constexpr float STATE_TOL = 1e-6f;   // absolute for state elements
static constexpr float P_REL_TOL = 1e-3f;   // relative for P entries
static constexpr float P_ABS_FLOOR = 1e-10f; // absolute floor for tiny P entries

void setUp(void) {}
void tearDown(void) {}

//==============================================================================
// Frozen dense reference implementation
//==============================================================================

namespace dense_ref {

static constexpr float SINGULARITY_THRESHOLD = 0.001f;

/// Gauss-Jordan 4x4 inversion (identical to EKF6::invert4x4)
static bool invert4x4(const float (*A)[M], float (*A_inv)[M]) {
    float work[M][M * 2];
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) {
            work[i][j] = A[i][j];
            work[i][j + M] = (i == j) ? 1.0f : 0.0f;
        }
    }

    for (int col = 0; col < M; col++) {
        int max_row = col;
        float max_val = std::fabs(work[col][col]);
        for (int row = col + 1; row < M; row++) {
            float val = std::fabs(work[row][col]);
            if (val > max_val) {
                max_val = val;
                max_row = row;
            }
        }

        if (max_val < 1e-10f) return false;

        if (max_row != col) {
            for (int j = 0; j < M * 2; j++) {
                float tmp = work[col][j];
                work[col][j] = work[max_row][j];
                work[max_row][j] = tmp;
            }
        }

        float pivot = work[col][col];
        for (int j = 0; j < M * 2; j++) {
            work[col][j] /= pivot;
        }

        for (int row = 0; row < M; row++) {
            if (row != col) {
                float factor = work[row][col];
                for (int j = 0; j < M * 2; j++) {
                    work[row][j] -= factor * work[col][j];
                }
            }
        }
    }

    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) {
            A_inv[i][j] = work[i][j + M];
        }
    }
    return true;
}

/**
 * Dense predict — frozen copy of the original EKF6::predict() before
 * sparsity optimization. All matrix operations are triple-nested loops.
 */
static void predict(float x[N], float P[N][N], const float Q[N],
                    float p, float q, float r, float dt) {
    const float phi = x[0];
    const float theta = x[1];
    const float bp = x[3];
    const float bq = x[4];
    const float br = x[5];

    const float p_corr = p - bp;
    const float q_corr = q - bq;
    const float r_corr = r - br;

    const float sph = std::sin(phi);
    const float cph = std::cos(phi);
    float cth = std::cos(theta);

    if (std::fabs(cth) < SINGULARITY_THRESHOLD) {
        cth = (cth >= 0.0f) ? SINGULARITY_THRESHOLD : -SINGULARITY_THRESHOLD;
    }

    const float sth = std::sin(theta);
    const float tth = sth / cth;

    const float phi_dot = p_corr + q_corr * sph * tth + r_corr * cph * tth;
    const float theta_dot = q_corr * cph - r_corr * sph;

    x[0] = phi + dt * phi_dot;
    x[1] = theta + dt * theta_dot;

    // Build F = I + dt * A
    float F[N][N];
    std::memset(F, 0, sizeof(F));
    for (int i = 0; i < N; i++) F[i][i] = 1.0f;

    F[0][0] = 1.0f + dt * (q_corr * cph * tth - r_corr * sph * tth);
    const float sec2th = 1.0f + tth * tth;
    F[0][1] = dt * (q_corr * sph * sec2th + r_corr * cph * sec2th);
    F[0][3] = -dt;
    F[0][4] = -dt * sph * tth;
    F[0][5] = -dt * cph * tth;

    F[1][0] = dt * (-q_corr * sph - r_corr * cph);
    F[1][4] = -dt * cph;
    F[1][5] = dt * sph;

    // FP = F * P (dense)
    float FP[N][N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            FP[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                FP[i][j] += F[i][k] * P[k][j];
            }
        }
    }

    // P_new = FP * F' + Q (dense)
    float P_new[N][N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            P_new[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                P_new[i][j] += FP[i][k] * F[j][k];
            }
            if (i == j) P_new[i][j] += Q[i];
        }
    }

    std::memcpy(P, P_new, sizeof(P_new));
}

/**
 * Dense correct — frozen copy of the original EKF6::correct() before
 * sparsity optimization. All matrix operations are triple-nested loops.
 */
static void correct(float x[N], float P[N][N], const float R[M],
                    float ax, float ay, float az, float gamma, float g) {
    const float phi = x[0];
    const float theta = x[1];
    const float alpha = x[2];

    const float sph = std::sin(phi);
    const float cph = std::cos(phi);
    const float sth = std::sin(theta);
    const float cth = std::cos(theta);

    float z_pred[M];
    z_pred[0] = g * sth;
    z_pred[1] = -g * cth * sph;
    z_pred[2] = -g * cth * cph;
    z_pred[3] = alpha;

    float H[M][N];
    std::memset(H, 0, sizeof(H));
    H[0][1] = g * cth;
    H[1][0] = -g * cth * cph;
    H[1][1] = g * sth * sph;
    H[2][0] = g * cth * sph;
    H[2][1] = g * sth * cph;
    H[3][2] = 1.0f;

    const float alpha_meas = theta - gamma;
    float z[M] = {ax, ay, az, alpha_meas};

    float y[M];
    for (int i = 0; i < M; i++) y[i] = z[i] - z_pred[i];

    // HP = H * P (dense)
    float HP[M][N];
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            HP[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                HP[i][j] += H[i][k] * P[k][j];
            }
        }
    }

    // S = HP * H' + R (dense)
    float S[M][M];
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < M; j++) {
            S[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                S[i][j] += HP[i][k] * H[j][k];
            }
            if (i == j) S[i][j] += R[i];
        }
    }

    float S_inv[M][M];
    if (!invert4x4(S, S_inv)) return;

    // PHt = P * H' (dense)
    float PHt[N][M];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            PHt[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                PHt[i][j] += P[i][k] * H[j][k];
            }
        }
    }

    // K = PHt * S_inv (dense)
    float K[N][M];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            K[i][j] = 0.0f;
            for (int k = 0; k < M; k++) {
                K[i][j] += PHt[i][k] * S_inv[k][j];
            }
        }
    }

    // State update: x += K * y
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            x[i] += K[i][j] * y[j];
        }
    }

    // IKH = I - K*H (dense)
    float IKH[N][N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            IKH[i][j] = (i == j) ? 1.0f : 0.0f;
            for (int k = 0; k < M; k++) {
                IKH[i][j] -= K[i][k] * H[k][j];
            }
        }
    }

    // IKHP = IKH * P (dense)
    float IKHP[N][N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            IKHP[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                IKHP[i][j] += IKH[i][k] * P[k][j];
            }
        }
    }

    // P_new = IKHP * IKH' + K * R * K' (dense, Joseph form)
    float P_new[N][N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            P_new[i][j] = 0.0f;
            for (int k = 0; k < N; k++) {
                P_new[i][j] += IKHP[i][k] * IKH[j][k];
            }
            for (int k = 0; k < M; k++) {
                P_new[i][j] += K[i][k] * R[k] * K[j][k];
            }
        }
    }

    std::memcpy(P, P_new, sizeof(P_new));
}

} // namespace dense_ref

//==============================================================================
// Comparison helpers
//==============================================================================

/// Compare state vectors element-by-element
static void compare_states(const float* prod_x, const float* ref_x,
                           int step, float tol) {
    for (int i = 0; i < N; i++) {
        float diff = std::fabs(prod_x[i] - ref_x[i]);
        if (diff > tol) {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                "State x[%d] diverged at step %d: prod=%e ref=%e diff=%e",
                i, step, (double)prod_x[i], (double)ref_x[i], (double)diff);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

/// Compare P matrices element-by-element (relative tolerance with absolute floor)
static void compare_P(const float (*prod_P)[N], const float (*ref_P)[N],
                      int step, float rel_tol, float abs_floor) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            float diff = std::fabs(prod_P[i][j] - ref_P[i][j]);
            float scale = std::fabs(ref_P[i][j]);
            float threshold = (scale > abs_floor) ? rel_tol * scale : abs_floor;
            if (diff > threshold) {
                char msg[128];
                std::snprintf(msg, sizeof(msg),
                    "P[%d][%d] diverged at step %d: prod=%e ref=%e diff=%e",
                    i, j, step, (double)prod_P[i][j], (double)ref_P[i][j],
                    (double)diff);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

//==============================================================================
// Test scenarios
//==============================================================================

/**
 * Scenario 1: Pitch rate ramp
 * 5 deg/s pitch rate for 2 seconds, then hold for 1 second.
 */
void test_sparsity_pitch_rate(void) {
    float pitch_rate = 5.0f * DEG2RAD;

    // Production EKF6
    EKF6 prod;
    prod.init();

    // Dense reference state
    float ref_x[N] = {0};
    float ref_P[N][N];
    std::memset(ref_P, 0, sizeof(ref_P));
    EKF6::Config cfg = EKF6::Config::defaults();
    ref_P[0][0] = cfg.p_attitude;
    ref_P[1][1] = cfg.p_attitude;
    ref_P[2][2] = cfg.p_alpha;
    ref_P[3][3] = cfg.p_bias;
    ref_P[4][4] = cfg.p_bias;
    ref_P[5][5] = cfg.p_bias;
    float Q[N] = {cfg.q_attitude, cfg.q_attitude, cfg.q_alpha,
                  cfg.q_bias, cfg.q_bias, cfg.q_bias};
    float R[M] = {cfg.r_accel, cfg.r_accel, cfg.r_accel, cfg.r_alpha};

    float theta_true = 0.0f;
    int n_samples = static_cast<int>(3.0f / DT);

    for (int i = 0; i < n_samples; i++) {
        float t = i * DT;
        float q_rate = (t < 2.0f) ? pitch_rate : 0.0f;
        if (t < 2.0f) theta_true += pitch_rate * DT;

        EKF6::Measurements meas = {
            .ax = G * std::sin(theta_true),
            .ay = 0.0f,
            .az = -G * std::cos(theta_true),
            .p = 0.0f,
            .q = q_rate,
            .r = 0.0f,
            .gamma = 0.0f
        };

        // Production
        prod.update(meas, DT);

        // Dense reference
        dense_ref::predict(ref_x, ref_P, Q, 0.0f, q_rate, 0.0f, DT);
        dense_ref::correct(ref_x, ref_P, R,
                           meas.ax, meas.ay, meas.az, 0.0f, G);

        // Compare
        compare_states(prod.getX(), ref_x, i, STATE_TOL);
        compare_P(prod.getP(), ref_P, i, P_REL_TOL, P_ABS_FLOOR);
    }
}

/**
 * Scenario 2: Banked flight
 * 30 deg bank with coordinated turn rate.
 */
void test_sparsity_banked_flight(void) {
    float phi_true = 30.0f * DEG2RAD;
    // Coordinated turn: r = g*tan(phi)/V, but for simplicity just use static bank
    float ax = 0.0f;
    float ay = -G * std::sin(phi_true);
    float az = -G * std::cos(phi_true);

    EKF6 prod;
    prod.init();

    float ref_x[N] = {0};
    float ref_P[N][N];
    std::memset(ref_P, 0, sizeof(ref_P));
    EKF6::Config cfg = EKF6::Config::defaults();
    ref_P[0][0] = cfg.p_attitude;
    ref_P[1][1] = cfg.p_attitude;
    ref_P[2][2] = cfg.p_alpha;
    ref_P[3][3] = cfg.p_bias;
    ref_P[4][4] = cfg.p_bias;
    ref_P[5][5] = cfg.p_bias;
    float Q[N] = {cfg.q_attitude, cfg.q_attitude, cfg.q_alpha,
                  cfg.q_bias, cfg.q_bias, cfg.q_bias};
    float R[M] = {cfg.r_accel, cfg.r_accel, cfg.r_accel, cfg.r_alpha};

    int n_samples = static_cast<int>(2.0f / DT);

    for (int i = 0; i < n_samples; i++) {
        EKF6::Measurements meas = {
            .ax = ax, .ay = ay, .az = az,
            .p = 0.0f, .q = 0.0f, .r = 0.0f,
            .gamma = 0.0f
        };

        prod.update(meas, DT);

        dense_ref::predict(ref_x, ref_P, Q, 0.0f, 0.0f, 0.0f, DT);
        dense_ref::correct(ref_x, ref_P, R, ax, ay, az, 0.0f, G);

        compare_states(prod.getX(), ref_x, i, STATE_TOL);
        compare_P(prod.getP(), ref_P, i, P_REL_TOL, P_ABS_FLOOR);
    }
}

/**
 * Scenario 3: Combined maneuver
 * Simultaneous pitch, roll, and yaw rates with gyro bias.
 * This exercises all F and H matrix entries.
 */
void test_sparsity_combined_maneuver(void) {
    EKF6 prod;
    prod.init();

    float ref_x[N] = {0};
    float ref_P[N][N];
    std::memset(ref_P, 0, sizeof(ref_P));
    EKF6::Config cfg = EKF6::Config::defaults();
    ref_P[0][0] = cfg.p_attitude;
    ref_P[1][1] = cfg.p_attitude;
    ref_P[2][2] = cfg.p_alpha;
    ref_P[3][3] = cfg.p_bias;
    ref_P[4][4] = cfg.p_bias;
    ref_P[5][5] = cfg.p_bias;
    float Q[N] = {cfg.q_attitude, cfg.q_attitude, cfg.q_alpha,
                  cfg.q_bias, cfg.q_bias, cfg.q_bias};
    float R[M] = {cfg.r_accel, cfg.r_accel, cfg.r_accel, cfg.r_alpha};

    int n_samples = static_cast<int>(3.0f / DT);

    for (int i = 0; i < n_samples; i++) {
        float t = i * DT;

        // Time-varying rates to exercise all Jacobian entries
        float p_rate = 3.0f * DEG2RAD * std::sin(2.0f * t);
        float q_rate = 5.0f * DEG2RAD * std::cos(1.5f * t);
        float r_rate = 2.0f * DEG2RAD * std::sin(t);
        float gamma = 2.0f * DEG2RAD * std::sin(0.5f * t);

        // Use the reference state to generate consistent accel measurements
        float sth = std::sin(ref_x[1]);
        float cth = std::cos(ref_x[1]);
        float sph = std::sin(ref_x[0]);
        float cph = std::cos(ref_x[0]);
        float ax = G * sth;
        float ay = -G * cth * sph;
        float az = -G * cth * cph;

        EKF6::Measurements meas = {
            .ax = ax, .ay = ay, .az = az,
            .p = p_rate, .q = q_rate, .r = r_rate,
            .gamma = gamma
        };

        prod.update(meas, DT);

        dense_ref::predict(ref_x, ref_P, Q, p_rate, q_rate, r_rate, DT);
        dense_ref::correct(ref_x, ref_P, R, ax, ay, az, gamma, G);

        compare_states(prod.getX(), ref_x, i, STATE_TOL);
        compare_P(prod.getP(), ref_P, i, P_REL_TOL, P_ABS_FLOOR);
    }
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_sparsity_pitch_rate);
    RUN_TEST(test_sparsity_banked_flight);
    RUN_TEST(test_sparsity_combined_maneuver);

    return UNITY_END();
}
