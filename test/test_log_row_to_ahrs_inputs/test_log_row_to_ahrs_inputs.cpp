#include <unity.h>
#include <cmath>

#include <replay/LogRowToAhrsInputs.h>

using onspeed::LogRow;
using onspeed::replay::LogRowToAhrsInputs;

void setUp(void) {}
void tearDown(void) {}

static LogRow makeRow(uint64_t ts_us, float pfwd_smoothed,
                      float fwdG, float latG, float vertG,
                      float rollDps, float pitchDps, float yawDps,
                      float ias_kt, float palt_ft, float oat_c) {
    LogRow r{};
    r.timeStampUs        = ts_us;
    r.pfwdSmoothed       = pfwd_smoothed;
    r.imuForwardG        = fwdG;
    r.imuLateralG        = latG;
    r.imuVerticalG       = vertG;
    r.imuRollRateDps     = rollDps;
    r.imuPitchRateDps    = pitchDps;
    r.imuYawRateDps      = yawDps;
    r.imuTempCelsius     = 25.0f;
    r.iasKt              = ias_kt;
    r.paltFt             = palt_ft;
    r.oatCelsius         = oat_c;
    return r;
}

void test_first_row_is_seed(void) {
    LogRowToAhrsInputs bridge;
    auto out = bridge.translate(makeRow(1000000, 100.0f,
                                         0.01f, 0.02f, 1.0f,
                                         0.1f, 0.2f, 0.3f,
                                         50.0f, 1000.0f, 15.0f));
    TEST_ASSERT_TRUE(out.isSeedFrame);
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 208.0f, out.dtSec);
    TEST_ASSERT_EQUAL_FLOAT(0.01f, out.inputs.imu.accelXG);
    TEST_ASSERT_EQUAL_FLOAT(1.0f,  out.inputs.imu.accelZG);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, out.inputs.sensors.iasKt);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, out.inputs.sensors.paltFt);
}

void test_dt_computed_from_timestamp_diff(void) {
    LogRowToAhrsInputs bridge;
    (void)bridge.translate(makeRow(1000000, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    auto out = bridge.translate(makeRow(1004808, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    TEST_ASSERT_FALSE(out.isSeedFrame);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4.808e-3f, out.dtSec);
}

void test_fresh_pressure_bumps_synth_timestamp(void) {
    LogRowToAhrsInputs bridge;
    auto a = bridge.translate(makeRow(1000000, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    auto b = bridge.translate(makeRow(1004808, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    auto c = bridge.translate(makeRow(1009616, 101.5f, 0,0,1, 0,0,0, 50, 1000, 15));
    // b: PfwdSmoothed unchanged → iasUpdateTimestampUs same as a
    TEST_ASSERT_EQUAL_UINT32(a.inputs.iasUpdateTimestampUs,
                             b.inputs.iasUpdateTimestampUs);
    // c: PfwdSmoothed changed → iasUpdateTimestampUs bumped by 20000 us
    TEST_ASSERT_EQUAL_UINT32(b.inputs.iasUpdateTimestampUs + 20000,
                             c.inputs.iasUpdateTimestampUs);
}

void test_reset_clears_state(void) {
    LogRowToAhrsInputs bridge;
    (void)bridge.translate(makeRow(1000000, 100.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    bridge.reset();
    auto out = bridge.translate(makeRow(5000000, 200.0f, 0,0,1, 0,0,0, 50, 1000, 15));
    TEST_ASSERT_TRUE(out.isSeedFrame);
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 208.0f, out.dtSec);
}

void test_pitch_rate_sign(void) {
    // LogCsv::FormatRow emits -imuPitchRateDps on the wire; ParseRowByIndex
    // re-flips on read so LogRow.imuPitchRateDps is in firmware-internal
    // (un-negated) convention. The bridge passes it through unchanged.
    LogRowToAhrsInputs bridge;
    auto out = bridge.translate(makeRow(1000000, 100.0f, 0,0,1,
                                         0.0f, +5.0f, 0.0f,
                                         50, 1000, 15));
    TEST_ASSERT_EQUAL_FLOAT(+5.0f, out.inputs.imu.gyroPitchDps);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_row_is_seed);
    RUN_TEST(test_dt_computed_from_timestamp_diff);
    RUN_TEST(test_fresh_pressure_bumps_synth_timestamp);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_pitch_rate_sign);
    return UNITY_END();
}
