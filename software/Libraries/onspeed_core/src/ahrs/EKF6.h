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
 * Sign conventions THIS FILTER expects on its inputs:
 * - Accelerometer: in level flight ax=0, ay=0, az=-g.
 *   (Gravity reaction force on the sensor; az is negative because the
 *   sensor's reaction-to-gravity vector points "up" along body -Z.)
 * - Gyro: +p means roll rate increases phi (right wing dropping further
 *   in OnSpeed convention, where right-wing-down is NEGATIVE phi after
 *   the accelRoll seed convention — see the conversion note below).
 *   +q means pitch rate increases theta (nose-up in standard aerospace
 *   convention, +theta = nose up). +r means yaw rate increases heading
 *   in the standard right-hand-rule sense.
 *
 * @section onspeed_convention_mismatch OnSpeed firmware-to-EKF6 sign mapping
 *
 * The OnSpeed firmware's published attitude/rate convention does NOT
 * match what this filter expects on its inputs. The mapping is:
 *
 * | Firmware-published | EKF6 input | Sign relationship |
 * |--------------------|------------|-------------------|
 * | accelVertComp_     | az         | Same (both -g for level)         |
 * | accelFwdComp_      | ax         | Same                             |
 * | accelLatComp_      | ay         | Same                             |
 * | imuRollRateDps     | p          | NEGATE on input                  |
 * | imuPitchRateDps    | q          | NEGATE on input                  |
 * | imuYawRateDps      | r          | Same                             |
 * | flightPathDeg      | gamma      | Same                             |
 *
 * Why p,q need negation but az and r do not:
 *
 * 1. OnSpeed's IMU pipeline uses internal sign conventions where
 *    +imuPitchRateDps means "nose-down" rotation rate and
 *    +imuRollRateDps means "right-wing-up" rotation rate (these are
 *    the IMU330 axis-mapped raw values). The firmware compensates by
 *    negating the CSV PitchRate column for display and by negating
 *    Madgwick's getPitch/getRoll output. Without input negation, EKF6
 *    would publish theta and phi in the OPPOSITE direction from the
 *    rest of the firmware — flying sensibly under static accel but
 *    inverting on any gyro-driven motion.
 *
 * 2. accel signs are correct as-is: OnSpeed's accelVertComp_ is
 *    already -g for level flight (the IMU returns accelZG = -1g for
 *    level after fAzSign adjustment, the rotation passes that through
 *    for cr*cp = 1, and the constructor seeds accelVertFilter_ to
 *    -1.0f). Same convention as EKF6 expects.
 *
 * 3. Yaw rate doesn't appear in the gravity-only accelerometer
 *    measurement equations the EKF6 sees (centripetal terms are
 *    pre-removed upstream). The Gen2 Octave reference also leaves
 *    YawRateDegic un-negated.
 *
 * The Ahrs adapter at sketch_common's call site applies the negations.
 * The EKF6 itself receives values in EKF6's own convention and runs
 * the standard Euler-angle kinematics on them. Bias states (bp, bq,
 * br) evolve in EKF6's convention too — so any code that reads
 * prevState.bq and feeds it back into a firmware-frame computation
 * needs the matching sign flip (Ahrs.cpp does this for bq in the
 * iasAlive-gated centripetal pre-comp).
 *
 * @section history Changelog
 *
 * - v1.0 (initial port): both az and p,q signs wrong (porting error
 *   from Gen2 Octave reference, which negated VerticalGic on input
 *   in Gen2's IMU convention but the same negation inverts in Gen3's
 *   convention). Filter ran into gimbal-lock for any non-level static
 *   input.
 * - PR #310: az sign corrected (Bug 1) + p,q signs corrected with
 *   coupled bias-correction sign flip (Bugs 2 + 3). Verified against
 *   Octave reference; static and dynamic sign tests added.
 *
 * Tuning is unchanged from the initial port and is heavily
 * accel-dominated (low r_accel relative to high q_attitude). Under
 * fast gyro-driven motion, EKF6 publishes attitude smaller in
 * magnitude than Madgwick. That's a separate tuning concern; out of
 * scope for the sign-bug PR.
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
        // Process noise variances (Q matrix diagonal)
        float q_attitude;   ///< Attitude process noise (rad^2), default 0.001
        float q_alpha;      ///< Alpha process noise (rad^2), default 0.0001
        float q_bias;       ///< Gyro bias drift ((rad/s)^2), default 1e-8

        // Measurement noise variances (R matrix diagonal)
        float r_accel;      ///< Accelerometer noise ((m/s^2)^2), default 0.5
        float r_alpha;      ///< Alpha measurement noise (rad^2), default 0.01

        // Initial state covariance (P0 matrix diagonal)
        float p_attitude;   ///< Initial attitude uncertainty (rad^2), default 0.1
        float p_alpha;      ///< Initial alpha uncertainty (rad^2), default 0.1
        float p_bias;       ///< Initial bias uncertainty ((rad/s)^2), default 0.01

        /**
         * @brief Get default configuration values
         * @return Config struct with production-ready defaults
         */
        static Config defaults() {
            return {
                0.001f,   // q_attitude - allows ~1.8 deg/s attitude change
                0.0001f,  // q_alpha - AOA changes slowly
                1e-8f,    // q_bias - biases very stable
                0.5f,     // r_accel - typical MEMS accelerometer noise
                0.01f,    // r_alpha - derived alpha has some uncertainty
                0.1f,     // p_attitude - moderate initial uncertainty (~18 deg)
                0.1f,     // p_alpha
                0.01f     // p_bias - start assuming small bias (~0.6 deg/s)
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
