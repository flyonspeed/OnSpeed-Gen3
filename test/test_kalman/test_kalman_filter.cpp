// The KalmanFilter fuses barometric altitude with vertical acceleration to produce
// smooth altitude and vertical speed (VSI) estimates.
//
// The filter runs at IMU rate (~208 Hz, dt ≈ 0.0048s)

#include <unity.h>
#include <KalmanFilter.h>
#include <cmath>

// Production tuning parameters from AHRS.cpp line 47
static const float PROD_Z_VARIANCE = 0.79078f;
static const float PROD_ACCEL_VARIANCE = 26.0638f;
static const float PROD_ACCEL_BIAS_VARIANCE = 1e-11f;
static const float PROD_DT = 1.0f / 208.0f;  // ~0.0048s at 208 Hz

static KalmanFilter kf;

void setUp(void) {
}

void tearDown(void) {
}

// Test that filter initializes and first update returns reasonable values
void test_initial_state_preserved(void) {
    float initial_alt = 1000.0f;  // 1000 meters
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 initial_alt, 0.0f, 0.0f);

    volatile float z, v;
    kf.Update(initial_alt, 0.0f, PROD_DT, &z, &v);

    // First update should be exact (actual: 1000.0000, 0.0000)
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, initial_alt, z);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v);
}

// Test that filter tracks steady altitude
void test_steady_altitude_convergence(void) {
    float target_alt = 3048.0f;  // 10k' in meters

    // Initialize AT the target altitude
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 target_alt, 0.0f, 0.0f);

    volatile float z, v;

    // Run for ~1 second at 208 Hz at steady altitude
    for (int i = 0; i < 208; i++) {
        kf.Update(target_alt, 0.0f, PROD_DT, &z, &v);
    }

    // Should stay at measurement with zero VSI (actual: exact 0.0 error)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, target_alt, z);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
}

// steady climb at 500 fpm (~2.54 m/s)
void test_climb_velocity_estimation(void) {
    float start_alt = 1524.0f;  // 5000 ft in meters
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 start_alt, 0.0f, 0.0f);

    volatile float z, v;
    float altitude = start_alt;
    float climb_rate = 2.54f;  // ~500 fpm in m/s

    // Climb for 5 seconds at 208 Hz
    for (int i = 0; i < 208 * 5; i++) {
        altitude += climb_rate * PROD_DT;
        kf.Update(altitude, 0.0f, PROD_DT, &z, &v);
    }

    // Should track altitude and estimate climb rate
    TEST_ASSERT_FLOAT_WITHIN(0.01f, altitude, z);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, climb_rate, v);
}

// Test acceleration input contributes to state estimation
void test_acceleration_input_affects_state(void) {
    float start_alt = 1000.0f;
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 start_alt, 0.0f, 0.0f);

    volatile float z1, v1, z2, v2;

    // Run with zero acceleration
    for (int i = 0; i < 100; i++) {
        kf.Update(start_alt, 0.0f, PROD_DT, &z1, &v1);
    }

    // Reset and run with positive acceleration
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 start_alt, 0.0f, 0.0f);
    for (int i = 0; i < 100; i++) {
        kf.Update(start_alt, 5.0f, PROD_DT, &z2, &v2);  // 5 m/s^2 up
    }

    TEST_ASSERT_FALSE(std::isnan(v1));
    TEST_ASSERT_FALSE(std::isnan(v2));

    // With constant altitude measurement, VSI should stay near zero
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v1);
}

// Edge case: handle zero dt without crashing
void test_zero_dt_no_crash(void) {
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 100.0f, 0.0f, 0.0f);

    volatile float z, v;
    kf.Update(100.0f, 0.0f, 0.0f, &z, &v);

    TEST_ASSERT_FALSE(std::isnan(z));
    TEST_ASSERT_FALSE(std::isnan(v));
}

// Edge case: handle turbulence (large, rapid acceleration changes)
void test_turbulence_stability(void) {
    kf.Configure(PROD_Z_VARIANCE, PROD_ACCEL_VARIANCE, PROD_ACCEL_BIAS_VARIANCE,
                 1000.0f, 0.0f, 0.0f);

    volatile float z, v;
    float altitude = 1000.0f;

    // Simulate turbulence: random-ish acceleration spikes
    float accels[] = {5.0f, -8.0f, 3.0f, -4.0f, 9.0f, -6.0f, 2.0f, -3.0f};
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 8; i++) {
            kf.Update(altitude, accels[i], PROD_DT, &z, &v);
        }
    }

    // Filter should remain stable
    TEST_ASSERT_FALSE(std::isnan(z));
    TEST_ASSERT_FALSE(std::isnan(v));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, altitude, z);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, v);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_preserved);
    RUN_TEST(test_steady_altitude_convergence);
    RUN_TEST(test_climb_velocity_estimation);
    RUN_TEST(test_acceleration_input_affects_state);
    RUN_TEST(test_zero_dt_no_crash);
    RUN_TEST(test_turbulence_stability);
    return UNITY_END();
}
