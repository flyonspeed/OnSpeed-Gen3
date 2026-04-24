// RunningMean.h — bounded-size moving-average circular buffer.
//
// Header-only so it stays inlineable. Replaces the Arduino
// RunningAverage library for the narrow API the firmware actually
// uses: addValue / getFastAverage / clear. No stats, no partial
// windows, no min/max bookkeeping — add those only when a call site
// needs them.
//
// Legacy behavior preserved:
//   * Buffer pre-zeroed at construction.
//   * addValue overwrites the oldest cell and maintains a running sum.
//   * getFastAverage returns sum / count.
//   * count saturates at capacity.
//
// One intentional deviation from Arduino RunningAverage: on an empty
// buffer, getFastAverage returns 0 (not NaN). Every sketch call site
// calls addValue before ever reading the average, so the first frame
// always sees sum/1 = first-sample, and the NaN-vs-zero distinction
// never surfaces in practice. Returning 0 avoids NaN leaking into
// downstream math if a future caller ever reads before seeding.

#pragma once

namespace onspeed {

class RunningMean {
public:
    explicit RunningMean(int capacity)
        : capacity_(capacity > 1 ? capacity : 1)
        , count_(0)
        , index_(0)
        , sum_(0.0f)
        , buf_(new float[capacity_]())
    {}

    ~RunningMean() { delete[] buf_; }

    RunningMean(const RunningMean&) = delete;
    RunningMean& operator=(const RunningMean&) = delete;

    void addValue(float v) {
        sum_ -= buf_[index_];
        buf_[index_] = v;
        sum_ += v;
        index_++;
        if (index_ >= capacity_) index_ = 0;
        if (count_ < capacity_) count_++;
    }

    float getFastAverage() const {
        return (count_ == 0) ? 0.0f : sum_ / static_cast<float>(count_);
    }

    void clear() {
        for (int i = 0; i < capacity_; ++i) buf_[i] = 0.0f;
        count_ = 0;
        index_ = 0;
        sum_   = 0.0f;
    }

    int count()    const { return count_; }
    int capacity() const { return capacity_; }

private:
    int    capacity_;
    int    count_;
    int    index_;
    float  sum_;
    float* buf_;
};

} // namespace onspeed
