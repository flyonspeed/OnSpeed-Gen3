// RunningMedian.h — bounded-size circular buffer that returns the
// median of its current contents.
//
// Replaces the Arduino RunningMedian library for the narrow API the
// firmware actually uses: add / getMedian / clear. No quantiles, no
// predict(), no getMedianAverage — add those only when a call site
// needs them.
//
// Semantics locked to Rob Tillaart's Arduino library v0.3.10 so
// SensorIO's pressure-despiking pipeline produces identical outputs:
//
//   * capacity floor of 3 (MEDIAN_MIN_SIZE).
//   * add(v) overwrites the oldest slot; count saturates at capacity.
//   * getMedian returns NaN when empty; for odd count returns the
//     middle of the sorted values; for even count returns the average
//     of the two middles.
//   * Sort is lazy — deferred until the next getMedian() call after an
//     add().

#pragma once

#include <cmath>

namespace onspeed {

class RunningMedian {
public:
    static constexpr int kMinCapacity = 3;

    explicit RunningMedian(int capacity)
        : capacity_(capacity < kMinCapacity ? kMinCapacity : capacity)
        , count_(0)
        , index_(0)
        , sorted_(false)
        , values_(new float[capacity_]())
        , sortIdx_(new int[capacity_])
    {
        for (int i = 0; i < capacity_; ++i) sortIdx_[i] = i;
    }

    ~RunningMedian()
    {
        delete[] values_;
        delete[] sortIdx_;
    }

    RunningMedian(const RunningMedian&) = delete;
    RunningMedian& operator=(const RunningMedian&) = delete;

    void add(float v)
    {
        values_[index_++] = v;
        if (index_ >= capacity_) index_ = 0;
        if (count_ < capacity_) ++count_;
        sorted_ = false;
    }

    float getMedian()
    {
        if (count_ == 0) return std::nanf("");
        if (!sorted_) sort();
        const int mid = count_ / 2;
        if (count_ & 0x1) {
            return values_[sortIdx_[mid]];
        }
        return (values_[sortIdx_[mid]] + values_[sortIdx_[mid - 1]]) * 0.5f;
    }

    void clear()
    {
        count_  = 0;
        index_  = 0;
        sorted_ = false;
        for (int i = 0; i < capacity_; ++i) {
            values_[i]  = 0.0f;
            sortIdx_[i] = i;
        }
    }

    int count()    const { return count_; }
    int capacity() const { return capacity_; }

private:
    // Insertion-sort sortIdx_ by values_[sortIdx_[i]]. Matches Tillaart's
    // linear-search variant (the firmware does not call setSearchMode).
    void sort()
    {
        for (int i = 1; i < count_; ++i) {
            const int temp = sortIdx_[i];
            const float f  = values_[temp];
            int hi;
            if (f <= values_[sortIdx_[0]]) {
                hi = 0;
            } else {
                hi = i;
                while (hi > 0 && f < values_[sortIdx_[hi - 1]]) {
                    --hi;
                }
            }
            int k = i;
            while (k > hi) {
                sortIdx_[k] = sortIdx_[k - 1];
                --k;
            }
            sortIdx_[k] = temp;
        }
        sorted_ = true;
    }

    int    capacity_;
    int    count_;
    int    index_;
    bool   sorted_;
    float* values_;
    int*   sortIdx_;
};

} // namespace onspeed
