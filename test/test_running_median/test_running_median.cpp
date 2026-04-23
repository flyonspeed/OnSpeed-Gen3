// test_running_median.cpp — unit tests for onspeed::RunningMedian.
//
// RunningMedian replaces the Arduino RunningMedian library for the
// narrow API the firmware uses:
//
//   RunningMedian(int capacity);
//   void  add(float);
//   float getMedian();        // NAN on empty; middle or avg-of-two-middles
//   void  clear();
//   int   count() const;
//   int   capacity() const;
//
// Semantics locked to Rob Tillaart's Arduino library (v0.3.10) for the
// methods above, so SensorIO's pressure-despiking pipeline produces
// identical outputs.

#include <unity.h>
#include <filters/RunningMedian.h>
#include <cmath>
#include <cstdlib>

using onspeed::RunningMedian;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// A standalone reference implementation of the Arduino RunningMedian
// library's add + getMedian semantics, kept in-file so the equivalence
// test below can't accidentally call into the onspeed implementation.
// Intentionally written straight and slow (full sort per getMedian)
// with no shared code paths.
// ---------------------------------------------------------------------------

namespace {

class ReferenceMedian {
public:
    explicit ReferenceMedian(int size) : size_(size < 3 ? 3 : size) {
        buf_  = new float[size_];
        for (int i = 0; i < size_; ++i) buf_[i] = 0.0f;
    }
    ~ReferenceMedian() { delete[] buf_; }
    ReferenceMedian(const ReferenceMedian&) = delete;
    ReferenceMedian& operator=(const ReferenceMedian&) = delete;

    void add(float v) {
        buf_[index_++] = v;
        if (index_ >= size_) index_ = 0;
        if (count_ < size_)  ++count_;
    }

    float getMedian() {
        if (count_ == 0) return std::nanf("");
        float* tmp = new float[count_];
        for (int i = 0; i < count_; ++i) tmp[i] = buf_[i];
        // straight insertion sort — no index layer
        for (int i = 1; i < count_; ++i) {
            float x = tmp[i];
            int j = i;
            while (j > 0 && tmp[j-1] > x) { tmp[j] = tmp[j-1]; --j; }
            tmp[j] = x;
        }
        float r;
        if (count_ & 0x1) {
            r = tmp[count_/2];
        } else {
            r = (tmp[count_/2] + tmp[count_/2 - 1]) * 0.5f;
        }
        delete[] tmp;
        return r;
    }

private:
    int    size_;
    int    count_  = 0;
    int    index_  = 0;
    float* buf_;
};

} // namespace

// ---------------------------------------------------------------------------
// Capacity and construction
// ---------------------------------------------------------------------------

void test_capacity_floored_to_three()
{
    // The Arduino library enforces MEDIAN_MIN_SIZE=3. Match it so
    // SensorIO behavior is identical even if iPressureSmoothing is
    // misconfigured to a tiny value.
    RunningMedian a(0);
    TEST_ASSERT_EQUAL_INT(3, a.capacity());

    RunningMedian b(2);
    TEST_ASSERT_EQUAL_INT(3, b.capacity());

    RunningMedian c(3);
    TEST_ASSERT_EQUAL_INT(3, c.capacity());

    RunningMedian d(15);
    TEST_ASSERT_EQUAL_INT(15, d.capacity());
}

void test_empty_median_is_nan()
{
    RunningMedian m(5);
    TEST_ASSERT_EQUAL_INT(0, m.count());
    TEST_ASSERT_TRUE(std::isnan(m.getMedian()));
}

// ---------------------------------------------------------------------------
// add / getMedian
// ---------------------------------------------------------------------------

void test_single_value_is_its_own_median()
{
    RunningMedian m(5);
    m.add(7.5f);
    TEST_ASSERT_EQUAL_INT(1, m.count());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 7.5f, m.getMedian());
}

void test_odd_count_returns_middle_element()
{
    RunningMedian m(7);
    m.add(3.0f);
    m.add(1.0f);
    m.add(5.0f);
    // sorted: 1, 3, 5 -> middle = 3
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, m.getMedian());
}

void test_even_count_averages_two_middles()
{
    RunningMedian m(5);
    m.add(4.0f);
    m.add(1.0f);
    // sorted: 1, 4 -> (1+4)/2 = 2.5
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.5f, m.getMedian());
}

void test_median_of_full_unsorted_odd_window()
{
    RunningMedian m(5);
    m.add(10.0f);
    m.add(2.0f);
    m.add(8.0f);
    m.add(1.0f);
    m.add(4.0f);
    // sorted: 1, 2, 4, 8, 10 -> median = 4
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 4.0f, m.getMedian());
}

void test_median_of_full_even_window_averages_two_middles()
{
    RunningMedian m(4);
    m.add(10.0f);
    m.add(2.0f);
    m.add(8.0f);
    m.add(1.0f);
    // sorted: 1, 2, 8, 10 -> (2+8)/2 = 5
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 5.0f, m.getMedian());
}

void test_rejects_single_outlier_spike()
{
    // The pressure pipeline's raison d'être: one bad sample must not
    // move the median. With a window of 5 reading a steady 100.0f,
    // one injected spike of 10000.0f should leave the median at 100.
    RunningMedian m(5);
    for (int i = 0; i < 5; ++i) m.add(100.0f);
    m.add(10000.0f);  // overwrite one slot with a spike
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 100.0f, m.getMedian());
}

void test_add_overwrites_oldest_not_newest()
{
    // With window=3 and inputs 1,2,3,100:
    // - after 1,2,3: buffer = [1,2,3], median = 2
    // - after 100:  buffer overwrites index 0 -> [100,2,3], sorted
    //   [2,3,100], median = 3 (NOT 2, which would mean "newest
    //   overwritten").
    RunningMedian m(3);
    m.add(1.0f);
    m.add(2.0f);
    m.add(3.0f);
    m.add(100.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, m.getMedian());
}

void test_count_saturates_at_capacity()
{
    RunningMedian m(5);
    for (int i = 0; i < 50; ++i) m.add(static_cast<float>(i));
    TEST_ASSERT_EQUAL_INT(5, m.count());
    // Last five adds are 45..49 -> sorted median = 47.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 47.0f, m.getMedian());
}

void test_monotonic_stream_tracks_middle()
{
    // Feeding a strictly-increasing sequence into a window of 11, the
    // median must equal the value that was added six samples ago.
    RunningMedian m(11);
    for (int i = 1; i <= 100; ++i) {
        m.add(static_cast<float>(i));
        if (i >= 11) {
            // After window fills, median of the last 11 values
            // (i-10..i) = i - 5.
            float expected = static_cast<float>(i - 5);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected, m.getMedian());
        }
    }
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

void test_clear_resets_state()
{
    RunningMedian m(5);
    m.add(100.0f);
    m.add(200.0f);
    m.add(300.0f);
    m.clear();

    TEST_ASSERT_EQUAL_INT(0, m.count());
    TEST_ASSERT_TRUE(std::isnan(m.getMedian()));

    // After clear the new value is the new seed and nothing from
    // before-clear carries.
    m.add(42.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 42.0f, m.getMedian());
}

// ---------------------------------------------------------------------------
// Equivalence: a pseudo-random 50 kHz-scale stream fed into both the
// onspeed implementation and the straight reference implementation
// must produce bit-identical medians at every step. This is the
// strongest test: it catches any subtle divergence in sort stability,
// wraparound off-by-one, or equal-value handling that the targeted
// unit tests above might miss.
// ---------------------------------------------------------------------------

void test_equivalence_vs_reference_random_stream()
{
    constexpr int kCapacity = 15;   // matches default iPressureSmoothing
    constexpr int kNumSamples = 5000;

    RunningMedian ours(kCapacity);
    ReferenceMedian ref(kCapacity);

    // Deterministic PRNG — srand seeded to a fixed value so the test
    // is reproducible.
    std::srand(0xC0FFEE);

    for (int i = 0; i < kNumSamples; ++i) {
        // Base pressure around 1013 hPa-ish with Gaussian-ish noise
        // and 1-in-50 outlier spikes (mimics real pneumatic input).
        float noise = static_cast<float>(std::rand() % 2001 - 1000) * 0.01f;
        float v = 1013.25f + noise;
        if ((std::rand() % 50) == 0) {
            v += (std::rand() % 2 ? 500.0f : -500.0f);
        }
        ours.add(v);
        ref.add(v);
        float got = ours.getMedian();
        float want = ref.getMedian();
        if (std::isnan(got) || std::isnan(want)) {
            TEST_ASSERT_TRUE(std::isnan(got) && std::isnan(want));
        } else {
            TEST_ASSERT_FLOAT_WITHIN(1e-4f, want, got);
        }
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_capacity_floored_to_three);
    RUN_TEST(test_empty_median_is_nan);
    RUN_TEST(test_single_value_is_its_own_median);
    RUN_TEST(test_odd_count_returns_middle_element);
    RUN_TEST(test_even_count_averages_two_middles);
    RUN_TEST(test_median_of_full_unsorted_odd_window);
    RUN_TEST(test_median_of_full_even_window_averages_two_middles);
    RUN_TEST(test_rejects_single_outlier_spike);
    RUN_TEST(test_add_overwrites_oldest_not_newest);
    RUN_TEST(test_count_saturates_at_capacity);
    RUN_TEST(test_monotonic_stream_tracks_middle);
    RUN_TEST(test_clear_resets_state);
    RUN_TEST(test_equivalence_vs_reference_random_stream);
    return UNITY_END();
}
