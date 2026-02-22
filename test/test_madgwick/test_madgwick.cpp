// Tests for MadgwickFusion - AHRS attitude estimation
//
// The Madgwick filter fuses gyroscope and accelerometer data to estimate
// aircraft pitch and roll. This is used for:
// - Centripetal acceleration compensation
// - Earth-referenced vertical acceleration (for Kalman VSI)
// - Derived AOA calculation
//
// Production usage from AHRS.cpp:
//   MadgFilter.begin(208.0f, -SmoothedPitch, SmoothedRoll)
//   MadgFilter.UpdateIMU(RollRate, PitchRate, YawRate, AccelFwd, AccelLat, AccelVert)
//   pitch = -MadgFilter.getPitch()
//   roll = -MadgFilter.getRoll()

#include <unity.h>
#include <MadgwickFusion.h>
#include <cmath>

using onspeed::Madgwick;

static const float SAMPLE_FREQ = 208.0f;
static const float DEG2RAD = 3.14159265f / 180.0f;

static Madgwick madgwick;

void setUp(void) {
}

void tearDown(void) {
}

// initializing level produces level output
void test_level_initialization(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, madgwick.getPitch());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, madgwick.getRoll());
}

// Initialize with non-zero pitch (simulating aircraft on ground with nose-up attitude)
void test_pitched_initialization(void) {
    madgwick.begin(SAMPLE_FREQ, 5.0f, 0.0f);  // 5 degrees nose up

    float pitch = madgwick.getPitch();
    TEST_ASSERT_FALSE(std::isnan(pitch));

    // begin() converts pitch/roll to quaternion; verify it's in reasonable range
    TEST_ASSERT_TRUE(fabsf(pitch) < 10.0f);
}

// level flight with gravity pointing down stays level
void test_level_flight_stability(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    // Simulate 1 second of level flight (no gyro rates, gravity straight down)
    for (int i = 0; i < 208; i++) {
        madgwick.UpdateIMU(0.0f, 0.0f, 0.0f,   // gyro: deg/s
                           0.0f, 0.0f, -1.0f);  // accel: normalized gravity
    }

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, madgwick.getPitch());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, madgwick.getRoll());
}

// constant pitch rate rotation
// Simulates pitching up at 10 deg/s for 3 seconds (should pitch ~30 degrees)
void test_pitch_rate_integration(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    float pitch_rate = 10.0f;  // deg/s pitch up
    float time_sec = 3.0f;
    int iterations = (int)(SAMPLE_FREQ * time_sec);

    // The gyro input is in deg/s, and the filter integrates it
    for (int i = 0; i < iterations; i++) {
        // As we pitch up, gravity vector tilts forward
        float current_pitch_rad = (float)i / iterations * 30.0f * DEG2RAD;
        float ax = sinf(current_pitch_rad);
        float az = -cosf(current_pitch_rad);

        madgwick.UpdateIMU(0.0f, pitch_rate, 0.0f,  // pitching up at 10 deg/s
                           ax, 0.0f, az);            // gravity follows
    }

    // Filter converges to ~31.5 degrees
    float pitch = madgwick.getPitch();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 31.5f, fabsf(pitch));
}

// constant roll rate (15 deg/s for 2 seconds = 30 degrees)
void test_roll_rate_integration(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    float roll_rate = 15.0f;  // deg/s roll right
    int iterations = (int)(SAMPLE_FREQ * 2.0f);  // 2 seconds

    for (int i = 0; i < iterations; i++) {
        float current_roll_rad = (float)i / iterations * 30.0f * DEG2RAD;
        float ay = -sinf(current_roll_rad);
        float az = -cosf(current_roll_rad);

        madgwick.UpdateIMU(roll_rate, 0.0f, 0.0f,  // rolling right
                           0.0f, ay, az);
    }

    // Filter converges to ~30.7 degrees
    float roll = madgwick.getRoll();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.7f, fabsf(roll));
}

// Quaternion must always be normalized (unit quaternion)
void test_quaternion_remains_normalized(void) {
    madgwick.begin(SAMPLE_FREQ, 10.0f, 5.0f);

    // Run through various maneuvers
    for (int i = 0; i < 500; i++) {
        float phase = (float)i / 50.0f;
        madgwick.UpdateIMU(10.0f * sinf(phase), 5.0f * cosf(phase), 2.0f,
                           0.1f, -0.2f, -0.98f);
    }

    float w, x, y, z;
    madgwick.getQuaternion(&w, &x, &y, &z);
    float magnitude = sqrtf(w*w + x*x + y*y + z*z);

    // Quaternion should stay very close to unit length (actual: 0.998313)
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 1.0f, magnitude);
}

// zero acceleration (freefall) - should not crash, gyro-only integration
void test_zero_acceleration_handling(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    // Zero accel - filter should skip accelerometer correction
    madgwick.UpdateIMU(5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    TEST_ASSERT_FALSE(std::isnan(madgwick.getPitch()));
    TEST_ASSERT_FALSE(std::isnan(madgwick.getRoll()));
}

// high G maneuver (2g pull-up)
void test_high_g_stability(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    // 2g pull-up: double the normal gravity
    for (int i = 0; i < 208; i++) {
        madgwick.UpdateIMU(0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, -2.0f);
    }

    // Should still report level (just more G, not tilted)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, madgwick.getPitch());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, madgwick.getRoll());
}

// Verify radians accessor works correctly
void test_radians_accessors(void) {
    madgwick.begin(SAMPLE_FREQ, 0.0f, 0.0f);

    // Pitch up so we have non-zero values
    for (int i = 0; i < 500; i++) {
        madgwick.UpdateIMU(0.0f, 5.0f, 0.0f,
                           0.1f, 0.0f, -0.995f);
    }

    float pitch_deg = madgwick.getPitch();
    float pitch_rad = madgwick.getPitchRadians();

    // Radians should be degrees * pi/180
    TEST_ASSERT_FLOAT_WITHIN(0.001f, pitch_deg * DEG2RAD, pitch_rad);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_level_initialization);
    RUN_TEST(test_pitched_initialization);
    RUN_TEST(test_level_flight_stability);
    RUN_TEST(test_pitch_rate_integration);
    RUN_TEST(test_roll_rate_integration);
    RUN_TEST(test_quaternion_remains_normalized);
    RUN_TEST(test_zero_acceleration_handling);
    RUN_TEST(test_high_g_stability);
    RUN_TEST(test_radians_accessors);
    return UNITY_END();
}
