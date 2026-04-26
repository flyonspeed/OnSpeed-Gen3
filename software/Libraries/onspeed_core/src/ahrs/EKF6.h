#ifndef EKF6_H_
#define EKF6_H_

/**
 * @file EKF6.h
 * @brief 6-State Extended Kalman Filter for Attitude and AOA Estimation
 *
 * This EKF estimates aircraft attitude (roll/pitch), angle of attack (AOA),
 * and gyroscope biases by fusing accelerometer and gyroscope measurements.
 *
 * @section state_vector State Vector (6 states)
 *
 * The state vector x = [phi, theta, alpha, bp, bq, br]^T contains:
 *
 * | Index | Symbol | Description              | Units  |
 * |-------|--------|--------------------------|--------|
 * | 0     | phi    | Roll angle               | rad    |
 * | 1     | theta  | Pitch angle              | rad    |
 * | 2     | alpha  | Angle of attack          | rad    |
 * | 3     | bp     | Roll rate gyro bias      | rad/s  |
 * | 4     | bq     | Pitch rate gyro bias     | rad/s  |
 * | 5     | br     | Yaw rate gyro bias       | rad/s  |
 *
 * @section measurements Measurements (4 measurements)
 *
 * The measurement vector z = [ax, ay, az, alpha_meas]^T contains:
 *
 * | Index | Symbol     | Description                    | Units  |
 * |-------|------------|--------------------------------|--------|
 * | 0     | ax         | Forward accelerometer          | m/s^2  |
 * | 1     | ay         | Lateral accelerometer          | m/s^2  |
 * | 2     | az         | Vertical accelerometer         | m/s^2  |
 * | 3     | alpha_meas | Derived AOA = theta - gamma    | rad    |
 *
 * @section dynamics State Dynamics (Euler Angle Kinematics)
 *
 * The state transition equations are:
 *
 *   phi_dot   = (p - bp) + (q - bq)*sin(phi)*tan(theta) + (r - br)*cos(phi)*tan(theta)
 *   theta_dot = (q - bq)*cos(phi) - (r - br)*sin(phi)
 *   alpha_dot = 0  (assumed constant between updates)
 *   bp_dot    = 0  (bias drift modeled via process noise)
 *   bq_dot    = 0
 *   br_dot    = 0
 *
 * where p, q, r are gyroscope measurements (rad/s).
 *
 * @section measurement_model Measurement Model
 *
 * The accelerometer measures gravity in body frame (assuming negligible
 * aircraft acceleration):
 *
 *   ax_pred =  g * sin(theta)
 *   ay_pred = -g * cos(theta) * sin(phi)
 *   az_pred = -g * cos(theta) * cos(phi)
 *
 * The alpha measurement is derived from pitch angle and flight path angle:
 *
 *   alpha_meas = theta - gamma
 *
 * where gamma = asin(VSI / TAS) is the flight path angle.
 *
 * @section tuning Tuning Parameters
 *
 * Process noise (Q matrix diagonal):
 * - q_attitude: How much attitude can change per timestep (rad^2)
 * - q_alpha: How much AOA can change per timestep (rad^2)
 * - q_bias: How fast gyro biases can drift ((rad/s)^2)
 *
 * Measurement noise (R matrix diagonal):
 * - r_accel: Accelerometer measurement variance ((m/s^2)^2)
 * - r_alpha: Alpha measurement variance (rad^2)
 *
 * Higher process noise = filter trusts measurements more (faster response)
 * Higher measurement noise = filter trusts model more (smoother output)
 *
 * @section usage Usage Example
 *
 * @code
 * using namespace onspeed;
 *
 * EKF6 ekf;  // Uses default tuning
 * ekf.init(initial_phi, initial_theta);
 *
 * // In sensor loop at 208 Hz:
 * EKF6::Measurements meas = {
 *     .ax = accel_x_mps2,
 *     .ay = accel_y_mps2,
 *     .az = accel_z_mps2,
 *     .p = gyro_x_rps,
 *     .q = gyro_y_rps,
 *     .r = gyro_z_rps,
 *     .gamma = flight_path_angle_rad
 * };
 *
 * ekf.update(meas, dt);
 * EKF6::State state = ekf.getState();
 * float pitch_deg = state.theta_deg();
 * float aoa_deg = state.alpha_deg();
 * @endcode
 *
 * @section reference Reference Implementation
 *
 * The algorithm is validated against octave_reference/ekf6_reference.m
 * which generates reference CSV data for unit test comparison.
 *
 * @section coordinate_frames Coordinate Frame Convention
 *
 * Body frame (NED-like):
 * - X: Forward (positive out the nose)
 * - Y: Right wing (positive to starboard)
 * - Z: Down (positive toward ground)
 *
 * Sign conventions THIS FILTER expects on its inputs (standard
 * aerospace inertial-frame convention, NOT OnSpeed's pilot-facing
 * convention — see the next section for the mapping the Ahrs adapter
 * applies):
 * - Accelerometer: in level flight ax=0, ay=0, **az=-g** (the body is
 *   "accelerating up" relative to free fall, so az is negative in the
 *   inertial-frame view). NOTE: OnSpeed's CSV "VerticalG" column
 *   reports the OPPOSITE convention (+1g for level — the reaction
 *   force on the proof mass). The Ahrs adapter negates accelVertComp_
 *   on the way into this filter; see EKF6 measurement adapter in
 *   Ahrs.cpp.
 * - Gyro: +p increases phi (right-wing-down rate, standard). +q
 *   increases theta (nose-up rate, standard). +r increases yaw
 *   (right-turn rate). NOTE: OnSpeed's internal imuRollRateDps and
 *   imuPitchRateDps are in the OPPOSITE convention (+pitch rate =
 *   nose-down internally); the Ahrs adapter negates them on the way
 *   into this filter. Yaw rate is in the same convention; no flip.
 *
 * @section onspeed_convention_mismatch OnSpeed firmware-to-EKF6 sign mapping
 *
 * OnSpeed's firmware uses pilot-facing "reaction force" sign conventions
 * (what you'd read off the panel: VerticalG = +1g sitting on the ramp).
 * EKF6's measurement model uses the standard inertial-frame view
 * (az = -g for level — the body is "accelerating up" relative to free
 * fall). The Ahrs adapter at the EKF6 call site bridges the conventions:
 *
 * | OnSpeed (production) | EKF6 input | Sign relationship                |
 * |----------------------|------------|----------------------------------|
 * | accelVertComp_       | az         | NEGATE: +1g level → -1g level    |
 * | accelFwdComp_        | ax         | Same                             |
 * | accelLatComp_        | ay         | Same                             |
 * | imuRollRateDps       | p          | NEGATE on input                  |
 * | imuPitchRateDps      | q          | NEGATE on input                  |
 * | imuYawRateDps        | r          | Same                             |
 * | flightPathDeg        | gamma      | Same                             |
 *
 * Three negations, one rationale: every place where OnSpeed's internal
 * sign convention differs from standard aerospace gets flipped at the
 * Ahrs adapter. The reasons are independent though:
 *
 * 1. **az**: OnSpeed's accelVertComp_ is +1g for level flight — IMU
 *    returns accelZG = +1g (sensor reaction force on the proof mass
 *    along body-Z down), the rotation preserves it for cr*cp = 1, and
 *    the AHRS constructor seeds accelVertFilter_ to +1.0f to match.
 *    EKF6 expects az = -g for level. Negate at the adapter.
 *
 *    Ahrs.cpp's EarthVertG formula already encodes the +1g convention
 *    via its trailing `- 1.0f` (subtracts the level-state +1g to leave
 *    only the deviation from level). That's the load-bearing pin
 *    fixing OnSpeed at +1g for level; the negation here is what
 *    converts to EKF6's -1g view.
 *
 * 2. **p, q (roll, pitch rate)**: OnSpeed's internal IMU rates after
 *    axis sign-mapping (IMU330::Read) are "+gyroPitchDps means
 *    nose-DOWN" / "+gyroRollDps means right-wing-UP" — opposite to
 *    EKF6's `phi_dot = p + ...` and `theta_dot = q*cph - r*sph` which
 *    expect "+p increases phi (right-wing-down)" / "+q increases
 *    theta (nose-up)" in the standard aerospace sense. Madgwick
 *    handles the same mismatch via its OUTPUT negation
 *    (-madgwick_.getPitch() / -madgwick_.getRoll()) plus un-negated
 *    input; EKF6 doesn't negate output, so we negate the INPUT rates.
 *    Mirrors the Gen2 Octave reference's `-RollRateDegic` /
 *    `-PitchRateDegic` lines.
 *
 *    The CSV layer (LogCsv.cpp) emits PitchRate as -imuPitchRateDps so
 *    pilots reading the log see "+ = nose up" — same sign discipline
 *    as EKF6's, opposite of the internal raw value.
 *
 * 3. **r (yaw rate)**: no negation. Yaw rate doesn't appear in EKF6's
 *    gravity-only accelerometer measurement equations (centripetal
 *    terms are pre-removed upstream by AHRS). Sign-vs-firmware-
 *    convention only affects coupling of yaw rate into theta and phi
 *    via the Euler kinematic terms during turns; the Octave reference
 *    leaves YawRateDegic un-negated and we follow.
 *
 * Bias states (bp, bq) evolve in EKF6's standard convention because
 * that's what the input rates are after the negation. Any code that
 * reads prevState.bq and uses it in a firmware-frame computation
 * needs the matching sign flip (Ahrs.cpp does this for bq in the
 * iasAlive-gated centripetal pre-comp at fPitchRateForComp).
 *
 * Tuning is heavily accel-dominated (low r_accel relative to high
 * q_attitude). Under fast gyro-driven motion, EKF6 publishes
 * attitude smaller in magnitude than Madgwick. Tuning is a separate
 * concern from the sign convention plumbing documented above; see
 * issue #128 for the broader EKF6 architectural concerns (gyro bias
 * frozen at q_bias=1e-8, alpha measurement is a tautology).
 *
 * @author FlyOnSpeed / OnSpeed Project
 * @date 2025
 */

namespace onspeed {

class EKF6 {
public:
    /// Number of states in the filter
    static constexpr int N_STATES = 6;

    /// Number of measurements
    static constexpr int N_MEAS = 4;

    /**
     * @brief Configuration parameters for EKF tuning
     *
     * These parameters control the filter's behavior:
     * - Process noise (Q): Trust in the model vs measurements
     * - Measurement noise (R): Expected sensor noise levels
     * - Initial covariance (P0): Starting uncertainty
     */
    struct Config {
        // Process noise spectral densities (Q matrix diagonal). Units
        // are continuous-time: variance per second (rad²/s for
        // attitude/alpha, (rad/s)²/s for bias). The predict step
        // injects Q*dt of variance into P, so the per-second injection
        // rate is exactly Q regardless of the IMU step rate.
        float q_attitude;   ///< Attitude process noise (rad²/s)
        float q_alpha;      ///< Alpha process noise (rad²/s)
        float q_bias;       ///< Gyro bias drift ((rad/s)²/s)

        // Measurement noise variances (R matrix diagonal). Units are
        // discrete-time variance — these are the actual variance of a
        // single accel/alpha sample.
        float r_accel;      ///< Accelerometer noise ((m/s²)²)
        float r_alpha;      ///< Alpha measurement noise (rad²)

        // Initial state covariance (P0 matrix diagonal). Units are
        // discrete-time variance — initial state-estimate uncertainty.
        float p_attitude;   ///< Initial attitude uncertainty (rad²)
        float p_alpha;      ///< Initial alpha uncertainty (rad²)
        float p_bias;       ///< Initial bias uncertainty ((rad/s)²)

        /**
         * @brief Get default configuration values
         * @return Config struct with production-ready defaults
         *
         * Q values are continuous-time spectral densities. The
         * predict step injects Q*dt per call, so the per-second
         * variance injection is exactly Q regardless of IMU
         * cadence. **Do NOT scale Q by IMU rate when changing
         * cadence** — the dt factor in predict() handles it.
         * Change Q only to deliberately retune the filter's
         * aggressiveness.
         */
        static Config defaults() {
            return {
                0.208f,    // q_attitude (rad²/s)
                0.0208f,   // q_alpha (rad²/s)
                2.08e-3f,  // q_bias ((rad/s)²/s) — gyro bias learns in ~30 s
                0.5f,      // r_accel ((m/s²)²) — typical MEMS accel noise
                0.01f,     // r_alpha (rad²) — derived alpha uncertainty
                0.1f,      // p_attitude (rad²) — moderate initial (~18°)
                0.1f,      // p_alpha (rad²)
                0.01f      // p_bias ((rad/s)²) — small bias init (~0.6 deg/s)
            };
        }
    };

    /**
     * @brief Sensor measurements for one EKF update cycle
     *
     * All values should be in SI units (m/s^2, rad/s, rad).
     * Sign conventions follow the body frame definition above.
     */
    struct Measurements {
        float ax;       ///< Forward accelerometer (m/s^2), positive forward
        float ay;       ///< Lateral accelerometer (m/s^2), positive right
        float az;       ///< Vertical accelerometer (m/s^2), positive down
        float p;        ///< Roll rate (rad/s), positive right wing down
        float q;        ///< Pitch rate (rad/s), positive nose up
        float r;        ///< Yaw rate (rad/s), positive nose right
        float gamma;    ///< Flight path angle (rad), from asin(VSI/TAS)
    };

    /**
     * @brief State estimate output from the filter
     *
     * Contains current estimates for all 6 states plus convenience
     * methods for degree conversion.
     */
    struct State {
        float phi;      ///< Roll angle (rad)
        float theta;    ///< Pitch angle (rad)
        float alpha;    ///< Angle of attack (rad)
        float bp;       ///< Roll gyro bias (rad/s)
        float bq;       ///< Pitch gyro bias (rad/s)
        float br;       ///< Yaw gyro bias (rad/s)

        /// @name Degree Conversion Methods
        /// @{
        float phi_deg() const;      ///< Roll in degrees
        float theta_deg() const;    ///< Pitch in degrees
        float alpha_deg() const;    ///< AOA in degrees
        float bp_dps() const;       ///< Roll bias in deg/s
        float bq_dps() const;       ///< Pitch bias in deg/s
        float br_dps() const;       ///< Yaw bias in deg/s
        /// @}
    };

    /**
     * @brief Construct EKF6 with default configuration
     *
     * Calls init() automatically with zero initial attitude.
     */
    EKF6();

    /**
     * @brief Construct EKF6 with custom configuration
     *
     * @param cfg Custom tuning parameters
     *
     * Calls init() automatically with zero initial attitude.
     */
    explicit EKF6(const Config& cfg);

    /**
     * @brief Initialize or reset the filter state
     *
     * Resets state vector and covariance matrix. Call this:
     * - At startup with accelerometer-derived initial attitude
     * - After detecting a discontinuity (e.g., sensor dropout)
     * - To restart estimation from known attitude
     *
     * @param initial_phi Initial roll estimate (rad), default 0
     * @param initial_theta Initial pitch estimate (rad), default 0
     */
    void init(float initial_phi = 0.0f, float initial_theta = 0.0f);

    /**
     * @brief Perform one predict + update cycle
     *
     * Should be called once per sensor sample (typically 208 Hz).
     * Internally performs:
     * 1. Predict: Propagate state using gyro measurements
     * 2. Update: Correct state using accelerometer measurements
     *
     * @param meas Sensor measurements for this timestep
     * @param dt Time since last update (seconds), typically 1/208
     */
    void update(const Measurements& meas, float dt);

    /**
     * @brief Get current state estimate
     *
     * @return State struct with all 6 state estimates
     */
    State getState() const;

    /**
     * @brief Get current configuration
     *
     * @return Reference to Config struct
     */
    const Config& getConfig() const { return config_; }

    /**
     * @brief Reset alpha covariance to initial uncertainty
     *
     * Call this when transitioning from a regime where alpha was
     * unobservable (e.g., IAS < 25 kt, gamma forced to 0) to one
     * where real gamma measurements are available.  This resets
     * P_[2][2] to the initial p_alpha value and zeros all
     * off-diagonal correlations involving alpha, telling the filter
     * to re-learn alpha from measurements without disturbing the
     * alpha state estimate itself.
     *
     * Safe to call on an uninitialized filter (no-op).
     */
    void resetAlphaCovariance();

#ifdef UNIT_TEST
public:
    const float (*getP() const)[N_STATES] { return P_; }
    const float* getX() const { return x_; }
private:
#endif

private:
    Config config_;                         ///< Filter tuning parameters
    float x_[N_STATES];                     ///< State vector [phi, theta, alpha, bp, bq, br]
    float P_[N_STATES][N_STATES];           ///< State covariance matrix (6x6)
    float Q_[N_STATES];                     ///< Process noise (diagonal only)
    float R_[N_MEAS];                       ///< Measurement noise (diagonal only)
    bool initialized_;                      ///< True after init() called

    /**
     * @brief Prediction step - propagate state forward using gyros
     *
     * Implements Euler angle kinematics with bias correction.
     * Updates x_ (state) and P_ (covariance).
     *
     * @param p Roll rate measurement (rad/s)
     * @param q Pitch rate measurement (rad/s)
     * @param r Yaw rate measurement (rad/s)
     * @param dt Time step (seconds)
     */
    void predict(float p, float q, float r, float dt);

    /**
     * @brief Update step - correct state using accelerometers
     *
     * Implements standard Kalman update with accelerometer and
     * derived alpha measurements.
     *
     * @param ax Forward acceleration (m/s^2)
     * @param ay Lateral acceleration (m/s^2)
     * @param az Vertical acceleration (m/s^2)
     * @param gamma Flight path angle (rad)
     * @param g Gravity constant (m/s^2)
     */
    void correct(float ax, float ay, float az, float gamma, float g);

    /// Standard gravity (m/s^2)
    static constexpr float GRAVITY = 9.80665f;

    /// Threshold for tan(theta) singularity protection at ±90 deg
    static constexpr float SINGULARITY_THRESHOLD = 0.001f;

    /**
     * @brief Invert a 4x4 matrix using Gauss-Jordan elimination
     *
     * Used to compute S^{-1} in the Kalman gain calculation.
     * Uses partial pivoting for numerical stability.
     *
     * @param A Input matrix (4x4)
     * @param A_inv Output inverse matrix (4x4)
     * @return true if inversion succeeded, false if matrix is singular
     */
    static bool invert4x4(const float (*A)[N_MEAS], float (*A_inv)[N_MEAS]);
};

}  // namespace onspeed

#endif  // EKF6_H_
