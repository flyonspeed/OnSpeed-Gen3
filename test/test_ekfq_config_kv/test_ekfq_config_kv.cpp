#include <unity.h>
#include <string>
#include <vector>

#include <ahrs/EkfqConfigKv.h>

using onspeed::ahrs::ParseEkfqConfigKv;

void setUp(void) {}
void tearDown(void) {}

static std::vector<std::string> g_warnings;
static void capture_warn(const char* msg) {
    g_warnings.emplace_back(msg);
}

// 24 keys total: 20 EKFQ::Config + 4 EkfqPipeline::PipelineConfig.
static constexpr int kTotalKeys = 24;

void test_empty_file_yields_all_defaults(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    TEST_ASSERT_TRUE(ParseEkfqConfigKv("", ekfq, pipe, capture_warn));
    const auto refE = onspeed::EKFQ::Config::defaults();
    const auto refP = onspeed::ahrs::EkfqPipeline::PipelineConfig::defaults();
    TEST_ASSERT_EQUAL_FLOAT(refE.q_quat, ekfq.q_quat);
    TEST_ASSERT_EQUAL_FLOAT(refE.r_baro, ekfq.r_baro);
    TEST_ASSERT_EQUAL_FLOAT(refP.accelEmaAlpha, pipe.accelEmaAlpha);
    // All keys missing → one warning per key.
    TEST_ASSERT_EQUAL_INT(kTotalKeys, (int)g_warnings.size());
}

void test_single_override(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    const std::string text = "q_quat=1.5e-6\n";
    TEST_ASSERT_TRUE(ParseEkfqConfigKv(text, ekfq, pipe, capture_warn));
    TEST_ASSERT_EQUAL_FLOAT(1.5e-6f, ekfq.q_quat);
    // r_baro stays at default
    TEST_ASSERT_EQUAL_FLOAT(onspeed::EKFQ::Config::defaults().r_baro, ekfq.r_baro);
    // One override, rest missing.
    TEST_ASSERT_EQUAL_INT(kTotalKeys - 1, (int)g_warnings.size());
}

void test_full_override(void) {
    // All 24 keys → no missing-key warnings.
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    const std::string text =
        "q_quat=1.0e-6\nq_bias=0.05\nq_z=0.001\nq_vz=0.0001\n"
        "q_b_az=0.0001\nq_beta=1e-8\n"
        "r_ax=15.0\nr_ay=10.0\nr_az=12.0\nr_baro=5.0\n"
        "r_beta_prior=0.1\nr_bias_prior=0.0003\nk_beta_r=6.0\n"
        "p_quat=0.05\np_bias=0.01\np_z=100.0\np_vz=4.0\n"
        "p_b_az=1.0\np_beta=0.01\ntas_min_mps=12.0\n"
        "accel_ema_alpha=0.05\ncomp_fade_tau_sec=2.5\n"
        "ias_gate_rising_kt=33.0\ntasdot_ema_alpha=0.2\n";
    TEST_ASSERT_TRUE(ParseEkfqConfigKv(text, ekfq, pipe, capture_warn));
    TEST_ASSERT_EQUAL_FLOAT(1.0e-6f, ekfq.q_quat);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, ekfq.r_ax);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, pipe.accelEmaAlpha);
    TEST_ASSERT_EQUAL_FLOAT(33.0f, pipe.iasGateRisingKt);
    TEST_ASSERT_EQUAL_INT(0, (int)g_warnings.size());
}

void test_comments_and_blanks(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    const std::string text =
        "# comment\n"
        "\n"
        "q_quat=2.0e-6\n"
        "# another comment\n";
    TEST_ASSERT_TRUE(ParseEkfqConfigKv(text, ekfq, pipe, capture_warn));
    TEST_ASSERT_EQUAL_FLOAT(2.0e-6f, ekfq.q_quat);
}

void test_unknown_key_errors(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    TEST_ASSERT_FALSE(ParseEkfqConfigKv("q_typo=1.0\n", ekfq, pipe, capture_warn));
    TEST_ASSERT_GREATER_THAN_INT(0, (int)g_warnings.size());
}

void test_malformed_line_errors(void) {
    g_warnings.clear();
    onspeed::EKFQ::Config ekfq{};
    onspeed::ahrs::EkfqPipeline::PipelineConfig pipe{};
    TEST_ASSERT_FALSE(ParseEkfqConfigKv("q_quat\n", ekfq, pipe, capture_warn)); // no =
    TEST_ASSERT_FALSE(ParseEkfqConfigKv("q_quat=notanumber\n", ekfq, pipe, capture_warn));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_file_yields_all_defaults);
    RUN_TEST(test_single_override);
    RUN_TEST(test_full_override);
    RUN_TEST(test_comments_and_blanks);
    RUN_TEST(test_unknown_key_errors);
    RUN_TEST(test_malformed_line_errors);
    return UNITY_END();
}
