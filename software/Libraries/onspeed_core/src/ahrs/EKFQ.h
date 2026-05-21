#ifndef EKFQ_H_
#define EKFQ_H_

/**
 * @file EKFQ.h
 * @brief 11-state Quaternion EKF: attitude + sideslip + vertical channel.
 *
 * Fuses IMU + baro + a weak β=0 prior + weak gyro-bias priors into a
 * quaternion-based attitude estimate with an integrated vertical
 * channel (z, vz, b_az) and a sideslip state β. Defaults are tuned
 * against VN-300 truth (Optuna studies `ekfq_v15` / `ekfq_v16` over a
 * 79-minute testbed flight log with a cruise-AOA loss profile).
 *
 * @section state_vector State Vector (11 states)
 *
 *   x = [q0, q1, q2, q3, bp, bq, br, z, vz, b_az, β]ᵀ
 *
 * | Idx | Symbol  | Description                            | Units  |
 * |-----|---------|----------------------------------------|--------|
 * | 0–3 | qN      | Unit quaternion (NED body, Hamilton)   | —      |
 * | 4   | bp      | Roll-rate gyro bias                    | rad/s  |
 * | 5   | bq      | Pitch-rate gyro bias                   | rad/s  |
 * | 6   | br      | Yaw-rate gyro bias                     | rad/s  |
 * | 7   | z       | Pressure altitude (+up)                | m      |
 * | 8   | vz      | Vertical speed (+down, NED)            | m/s    |
 * | 9   | b_az    | Earth-vertical accel bias              | m/s²   |
 * | 10  | β       | Sideslip                               | rad    |
 *
 * Alpha (AOA) is NOT a state — it is a derived output from the universal
 * kinematic formula  α = atan2(sin θ, cos φ cos θ) + asin(corr)  using the
 * current (φ, θ, vz, TAS, β). See alphaKinematicRad() below.
 *
 * @section sensor_inputs Sensor inputs (and sign conventions)
 *
 * - Accel in m/s², standard NED-body (level flight gives az ≈ -9.806).
 *   Pipeline adapter negates the firmware "+1g level" reading.
 * - Gyro in rad/s, aerospace standard (+p = right-wing-down,
 *   +q = nose-up, +r = right-turn). Pipeline adapter negates p and q
 *   from the firmware's internal IMU convention.
 * - TAS in m/s. Pipeline FADES tas and tasDot to zero on the ground
 *   (comp_fade ramp-in) so pitot-noise centripetal doesn't contaminate
 *   the filter at rest.
 * - Baro altitude in meters (+up). Caller converts ft → m.
 *
 * @section integration How this filter slots into Ahrs::Step()
 *
 * EKFQ takes RAW post-EMA installation-bias-corrected accels — NOT
 * centripetal-pre-compensated upstream. The filter's correct() step
 * models centripetal and TASdot inside h(x), with H entries that couple
 * lateral-G residuals to br and vertical-G residuals to bq (so banked
 * turns and pull-ups directly constrain gyro-bias estimation).
 *
 * When this filter is the active algorithm, the standalone altitude
 * KalmanFilter is bypassed — EKFQ's z/vz come from the same unified
 * state, giving one consistent vertical channel instead of two filters
 * disagreeing about altitude.
 *
 * @section embedded_perf ESP32-S3 performance notes
 *
 *   • predict():  F is built sparsely (bp/bq/br/b_az rows are identity).
 *     Covariance update F·P·Fᵀ skips the all-identity rows entirely —
 *     roughly 2× faster than the naïve 2·N³ approach for N=11.
 *   • correct():  pure BATCH update — 8 measurements (3 accel + baro
 *     + β prior + 3 gyro-bias priors), one joint Kalman gain K via
 *     Cholesky factorisation of S = H·P·Hᵀ + R, Joseph-form covariance
 *     update. Matches the Python `ekf_quat.py` reference exactly so
 *     the Optuna-tuned defaults give the same numerical behaviour the
 *     tuning study targeted. ~6–7k float ops/step including the
 *     11×11·11×11 Joseph matmuls; ESP32-S3 single-core overhead still
 *     under 2%.
 *
 * Memory: all stack. P is 11×11 = 484 bytes; the batch correct() step
 * needs ~2.4 KB of transient buffers (H, PHt, S, K_mat, KH, A); total
 * well inside the IMU task's stack budget. No heap, no exceptions.
 */

#include <cstdint>

namespace onspeed {

class EKFQ {
public:
    static constexpr int N_STATES = 11;

    /// State-vector indices.
    enum {
        Q0 = 0, Q1 = 1, Q2 = 2, Q3 = 3,
        BP_IDX = 4, BQ_IDX = 5, BR_IDX = 6,
        Z  = 7, VZ = 8, B_AZ = 9,
        BETA = 10
    };

    /**
     * @brief Filter tuning parameters.
     *
     * Defaults reproduce Optuna study `ekfq_v15` / `ekfq_v16` best trial
     * tuned against VN-300 truth on the testbed flight log with the
     * cruise-AOA loss profile (pitch + vz + explicit kinematic-α
     * residual).
     */
    struct Config {
        // Process noise (continuous spectral density).
        float q_quat;        ///< Per quaternion-component drift (rad²/s)
        float q_bias;        ///< Gyro bias drift ((rad/s)²/s)
        float q_z;           ///< Altitude state noise (m²/s)
        float q_vz;          ///< Vertical-speed noise ((m/s)²/s)
        float q_b_az;        ///< Vertical-accel bias drift ((m/s²)²/s)
        float q_beta;        ///< Sideslip random walk (rad²/s)

        // Measurement noise (discrete variance).
        float r_ax;          ///< Forward-accel noise ((m/s²)²)
        float r_ay;          ///< Lateral-accel noise ((m/s²)²)
        float r_az;          ///< Vertical-accel noise ((m/s²)²)
        float r_baro;        ///< Baro-altitude noise (m²)
        float r_beta_prior;  ///< β=0 weak-prior variance (rad²)
        float r_bias_prior;  ///< Gyro-bias prior variance ((rad/s)²)
        float k_beta_R;      ///< β-adaptive R_ay inflation
                             ///< (R_ay_eff = r_ay · (1 + k_beta_R · β²))

        // Initial covariance (P0 diagonal).
        float p_quat;        ///< Initial quaternion component variance
        float p_bias;        ///< Initial gyro-bias variance ((rad/s)²)
        float p_z;           ///< Initial altitude variance (m²)
        float p_vz;          ///< Initial vertical-speed variance ((m/s)²)
        float p_b_az;        ///< Initial vertical-accel bias variance ((m/s²)²)
        float p_beta;        ///< Initial β variance (rad²)

        /// Below this TAS, β dynamics are damped (low-speed taxi/stall).
        float tas_min_mps;

        /// Production-ready defaults from the Optuna best trial.
        static Config defaults();
    };

    /**
     * @brief Sensor measurements for one update cycle.
     *
     * Accels are RAW post-EMA installation-bias-corrected values — NOT
     * centripetal-compensated. EKFQ models centripetal inside h(x).
     * tasMps and tasDotMps2 must be faded to zero on the ground by the
     * caller (the Ahrs compFadeIn_ coefficient does this).
     */
    struct Measurements {
        float ax;             ///< Body-X accel, m/s² (NED, level ≈ 0)
        float ay;             ///< Body-Y accel, m/s²
        float az;             ///< Body-Z accel, m/s² (NED, level ≈ -g)
        float p;              ///< Roll rate, rad/s (aerospace +)
        float q;              ///< Pitch rate, rad/s (aerospace +)
        float r;              ///< Yaw rate, rad/s
        float baroAltMeters;  ///< Baro altitude, m (+up)
        float tasMps;         ///< TAS, m/s (faded to 0 on ground)
        float tasDotMps2;     ///< d(TAS)/dt, m/s² (faded to 0 on ground)
        bool  updateBaro;     ///< Skip baro update this cycle if false
    };

    /// Filter outputs in aerospace-engineer-friendly units.
    struct State {
        // Unit quaternion (Hamilton, [w, x, y, z]).
        float q0;
        float q1;
        float q2;
        float q3;
        // Gyro biases (rad/s).
        float bp;
        float bq;
        float br;
        // Vertical channel.
        float z;          ///< Altitude (m, +up)
        float vz;         ///< Vertical speed (m/s, +down)
        float b_az;       ///< Vertical-accel bias (m/s²)
        // Sideslip (rad).
        float beta;

        float roll_rad()  const;
        float pitch_rad() const;
        float yaw_rad()   const;
        float roll_deg()  const;
        float pitch_deg() const;
        float yaw_deg()   const;
        float beta_deg()  const;
    };

    /// Construct with default tuning. Caller must call init() before update().
    EKFQ();
    explicit EKFQ(const Config& cfg);

    /**
     * @brief Initialise / reset the filter.
     * @param initial_phi    Initial roll  (rad)
     * @param initial_theta  Initial pitch (rad)
     * @param initial_z      Initial altitude (m, +up)
     */
    void init(float initial_phi = 0.0f, float initial_theta = 0.0f,
              float initial_z = 0.0f);

    /// One predict + correct cycle. Call at IMU rate (~208 Hz).
    /// Convenience wrapper around predict() + correct().
    void update(const Measurements& meas, float dt);

    /// Propagate state and covariance forward one step.  `tas` is the
    /// un-faded TAS used to gate beta-dynamics activation
    /// (`tas > tas_min_mps`).
    void predict(float p, float q, float r,
                 float ax, float ay, float az,
                 float tas, float dt);

    /// Apply the measurement update.  `tas` and `tasDot` should be
    /// faded by the pipeline's compFadeIn before this call so the
    /// centripetal / TASdot terms in h(x) ramp in smoothly after the
    /// iasGate rising edge.
    void correct(float ax, float ay, float az,
                 float tas, float tasDot,
                 float pitchRate, float yawRate,
                 float baroZ, bool updateBaro);

    /// Snapshot current state.
    State getState() const;

    /// Compute derived kinematic AOA from current state + given TAS (rad).
    float alphaKinematicRad(float tas_mps) const;

    /// Reference to the loaded config.
    const Config& getConfig() const { return config_; }

    /// Lifetime count of update() invocations. Monotonic across init().
    uint32_t getUpdateCallCount() const { return updateCallCount_; }

    /// Number of correct() calls aborted at the Cholesky guard.
    /// Monotonic across init() so a pre-reseed burst stays visible.
    uint32_t getFailedUpdateCount() const { return failedUpdateCount_; }

    /// Value of getUpdateCallCount() at the most recent failure; 0 if none.
    uint32_t getLastFailedCallNum() const { return lastFailedCallNum_; }

    /// Replace the config. Does NOT reset state — call init() if needed.
    void setConfig(const Config& cfg) { config_ = cfg; }

    /**
     * @brief Reset only the vertical-channel covariance.
     *
     * Use when the baro becomes valid after a long dropout — re-seeds z
     * from baro and inflates P[z][z], P[vz][vz], P[b_az][b_az] back to
     * the initial values so the filter re-learns those states without
     * disturbing attitude/biases.
     */
    void resetVerticalCovariance(float baro_z);

#ifdef UNIT_TEST
public:
    const float (*getP() const)[N_STATES] { return P_; }
    const float* getX() const { return x_; }
private:
#endif

private:
    Config config_;
    float  x_[N_STATES];
    float  P_[N_STATES][N_STATES];
    bool   initialized_;

    // Counters survive init() — a failure burst before a reseed stays visible.
    // See issue #593 item #1.
    uint32_t updateCallCount_   = 0;
    uint32_t failedUpdateCount_ = 0;
    uint32_t lastFailedCallNum_ = 0;

    static constexpr float GRAVITY = 9.80665f;

    /// Renormalise the quaternion sub-vector after predict.
    void renormaliseQuaternion();

    /// Build a unit quaternion from Tait-Bryan angles (yaw forced to 0).
    static void eulerToQuat(float roll, float pitch, float yaw,
                            float out[4]);
};

}  // namespace onspeed

#endif  // EKFQ_H_
