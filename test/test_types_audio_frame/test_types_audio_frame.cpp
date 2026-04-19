// test_types_audio_frame.cpp — structural tests for onspeed::AudioFrame

#include <unity.h>
#include <types/AudioFrame.h>

using onspeed::AudioFrame;

void setUp(void) {}
void tearDown(void) {}

void test_default_initializes_to_empty_frame(void)
{
    AudioFrame f;
    TEST_ASSERT_NULL(f.samples);
    TEST_ASSERT_EQUAL_size_t(0u, f.sampleCount);
    TEST_ASSERT_EQUAL_INT(16000, f.sampleRateHz);
}

void test_fields_are_writable(void)
{
    static int16_t buf[4] = {100, -100, 200, -200};

    AudioFrame f;
    f.samples     = buf;
    f.sampleCount = 2u;    // 2 stereo frames (4 int16 values total)
    f.sampleRateHz = 16000;

    TEST_ASSERT_EQUAL_PTR(buf, f.samples);
    TEST_ASSERT_EQUAL_size_t(2u, f.sampleCount);
    TEST_ASSERT_EQUAL_INT(16000, f.sampleRateHz);
}

void test_size_is_reasonable(void)
{
    // pointer (8 bytes on 64-bit) + size_t (8 bytes) + int (4 bytes) = 20 bytes.
    // Allow padding to 32.
    TEST_ASSERT_LESS_OR_EQUAL(32u, sizeof(AudioFrame));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_initializes_to_empty_frame);
    RUN_TEST(test_fields_are_writable);
    RUN_TEST(test_size_is_reasonable);
    return UNITY_END();
}
