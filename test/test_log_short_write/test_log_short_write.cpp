// test_log_short_write.cpp — unit tests for ConsumeAlignedWrite.
//
// The helper sits between LogSensor's staging buffer and SdFat. It
// decides what to keep in the buffer after a (possibly short) write,
// so the writer never silently skips past missing bytes.

#include <unity.h>

#include <cstring>
#include <log/ConsumeAlignedWrite.h>

using onspeed::log::ConsumeAlignedWrite;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Fill buf with a known byte pattern so reordering / loss is detectable.
static void Fill(char *buf, size_t n)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = static_cast<char>('A' + (i % 26));
}

// ---------------------------------------------------------------------------
// Full-write path
// ---------------------------------------------------------------------------

void test_full_write_with_no_tail_clears_buffer()
{
    char   buf[64];
    Fill(buf, 32);
    size_t used = 32;

    bool ok = ConsumeAlignedWrite(32, 32, buf, sizeof(buf), &used);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0, used);
}

void test_full_write_with_tail_shifts_tail_to_front()
{
    char   buf[64];
    Fill(buf, 50);                 // "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWX"
    size_t used = 50;

    // Pretend we flushed the first 32 bytes; the remaining 18 are the post-flush tail
    bool ok = ConsumeAlignedWrite(32, 32, buf, sizeof(buf), &used);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(18, used);

    // The bytes at the front are now the original bytes 32..49 — 'G' onwards
    char expected[18];
    for (size_t i = 0; i < 18; i++)
        expected[i] = static_cast<char>('A' + ((32 + i) % 26));
    TEST_ASSERT_EQUAL_MEMORY(expected, buf, 18);
}

void test_full_write_of_entire_buffer()
{
    char   buf[64];
    Fill(buf, 64);
    size_t used = 64;

    bool ok = ConsumeAlignedWrite(64, 64, buf, sizeof(buf), &used);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0, used);
}

// ---------------------------------------------------------------------------
// Short-write path
// ---------------------------------------------------------------------------

void test_zero_byte_write_keeps_buffer_intact()
{
    char   buf[64];
    Fill(buf, 32);
    char   snapshot[32];
    std::memcpy(snapshot, buf, 32);
    size_t used = 32;

    bool ok = ConsumeAlignedWrite(32, 0, buf, sizeof(buf), &used);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(32, used);
    // Nothing was written, so the buffer must still hold every byte.
    TEST_ASSERT_EQUAL_MEMORY(snapshot, buf, 32);
}

void test_half_write_leaves_unwritten_half_at_front()
{
    char   buf[64];
    Fill(buf, 32);
    size_t used = 32;

    // SdFat accepted only the first 16 bytes; the back 16 of the
    // flush window must be retried.
    bool ok = ConsumeAlignedWrite(32, 16, buf, sizeof(buf), &used);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(16, used);

    // Front of buffer is the unwritten head: original bytes 16..31
    char expected[16];
    for (size_t i = 0; i < 16; i++)
        expected[i] = static_cast<char>('A' + ((16 + i) % 26));
    TEST_ASSERT_EQUAL_MEMORY(expected, buf, 16);
}

void test_off_by_one_short_write()
{
    char   buf[64];
    Fill(buf, 32);
    size_t used = 32;

    bool ok = ConsumeAlignedWrite(32, 31, buf, sizeof(buf), &used);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(1, used);

    // Exactly one byte left — the final byte of the flush window.
    TEST_ASSERT_EQUAL_CHAR(static_cast<char>('A' + (31 % 26)), buf[0]);
}

void test_short_write_preserves_post_flush_tail()
{
    char   buf[128];
    Fill(buf, 80);
    size_t used = 80;

    // Flush window is the first 48 bytes. SdFat accepted 32.
    // Remaining layout in szBuf: head (16) + tail (32) = 48 bytes.
    bool ok = ConsumeAlignedWrite(48, 32, buf, sizeof(buf), &used);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(48, used);

    // Unwritten head: original bytes 32..47
    char expectedHead[16];
    for (size_t i = 0; i < 16; i++)
        expectedHead[i] = static_cast<char>('A' + ((32 + i) % 26));
    TEST_ASSERT_EQUAL_MEMORY(expectedHead, buf, 16);

    // Post-flush tail: original bytes 48..79
    char expectedTail[32];
    for (size_t i = 0; i < 32; i++)
        expectedTail[i] = static_cast<char>('A' + ((48 + i) % 26));
    TEST_ASSERT_EQUAL_MEMORY(expectedTail, buf + 16, 32);
}

// ---------------------------------------------------------------------------
// Defensive path
// ---------------------------------------------------------------------------

void test_actual_greater_than_requested_is_rejected()
{
    char   buf[64];
    Fill(buf, 32);
    char   snapshot[32];
    std::memcpy(snapshot, buf, 32);
    size_t used = 32;

    bool ok = ConsumeAlignedWrite(32, 64, buf, sizeof(buf), &used);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(32, used);
    TEST_ASSERT_EQUAL_MEMORY(snapshot, buf, 32);
}

void test_null_buf_is_rejected()
{
    size_t used = 0;
    TEST_ASSERT_FALSE(ConsumeAlignedWrite(16, 16, nullptr, 64, &used));
}

void test_null_used_ptr_is_rejected()
{
    char buf[16];
    TEST_ASSERT_FALSE(ConsumeAlignedWrite(16, 16, buf, sizeof(buf), nullptr));
}

// ---------------------------------------------------------------------------
// Iterative retry simulates the LogSensor writer loop
// ---------------------------------------------------------------------------

void test_iterative_retry_drains_buffer_after_short_writes()
{
    // Simulate the LogSensorCommitTask loop: each iteration the SD
    // layer accepts some prefix of the flush window; the helper keeps
    // the rest at the front for the next round. After enough rounds
    // the buffer drains completely with no byte loss.
    char   buf[256];
    Fill(buf, 128);
    size_t used = 128;

    // Round 1: ask to flush 64 bytes, SD only accepts 20.
    bool ok = ConsumeAlignedWrite(64, 20, buf, sizeof(buf), &used);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(108, used);

    // Round 2: ask to flush 64 again (head of unwritten + tail), SD accepts 64.
    ok = ConsumeAlignedWrite(64, 64, buf, sizeof(buf), &used);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(44, used);

    // Round 3: flush the rest cleanly.
    ok = ConsumeAlignedWrite(44, 44, buf, sizeof(buf), &used);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0, used);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_full_write_with_no_tail_clears_buffer);
    RUN_TEST(test_full_write_with_tail_shifts_tail_to_front);
    RUN_TEST(test_full_write_of_entire_buffer);
    RUN_TEST(test_zero_byte_write_keeps_buffer_intact);
    RUN_TEST(test_half_write_leaves_unwritten_half_at_front);
    RUN_TEST(test_off_by_one_short_write);
    RUN_TEST(test_short_write_preserves_post_flush_tail);
    RUN_TEST(test_actual_greater_than_requested_is_rejected);
    RUN_TEST(test_null_buf_is_rejected);
    RUN_TEST(test_null_used_ptr_is_rejected);
    RUN_TEST(test_iterative_retry_drains_buffer_after_short_writes);

    return UNITY_END();
}
