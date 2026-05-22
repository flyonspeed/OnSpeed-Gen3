// SyntheticStream — Arduino Stream that emits pre-built byte frames on a
// fixed cadence, with task-notify wake fidelity.
//
// Perf-build-only (-DONSPEED_SYNTH_SENSORS=1). Constructed with one or more
// Frames (from onspeed_core/test_frames/SynthFrames.h). The Stream rotates
// through the frame array on each emission. Skyview passes 2 frames (!1
// ADAHRS, !3 EMS alternating). VN-300 and Boom pass 1.
//
// Cadence model
// =============
// Earlier versions of SyntheticStream drove emission cadence from inside
// the consumer task by checking `millis() >= nextEmitMs_` on each
// available()/read() call. That had two problems for high-rate perf
// benchmarks:
//
//   - The synth-mode EfisRead/BoomRead tasks polled at a fixed `vTaskDelay`
//     period (10 ms for EfisRead, 5 ms for BoomRead). Cadence was capped
//     by the consumer's poll rate, NOT the configured emission period.
//     Above ~200 Hz the stream would silently emit fewer frames than its
//     configured rate, because the next frame couldn't ship until the
//     consumer next woke and started reading. `efis_read` looked artificially
//     well-behaved at high rates.
//   - The "Loops/s" PERF reading on EfisRead reflected the synth-poll
//     rate, not the frame rate — making the bench measurement of "how
//     much work per wake" misleading.
//
// Now: SyntheticStream owns an esp_timer configured with `periodUs_`
// (microsecond resolution).  The timer callback runs in the esp_timer
// dispatcher task, increments `pendingEmits_` atomically, and
// `xTaskNotifyGive`s the registered consumer task. The consumer task
// uses `ulTaskNotifyTake` to block on emit (mirroring the IDF UART
// event-queue wake-on-data pattern used in real-hardware EfisReadTask).
// When the consumer next reads via available()/read(), the lazy
// frame-load (see loadNextFrameIfPending()) moves bytes into the cursor.
// Only the consumer task touches the byte state, so no critical section
// is needed inside Stream::read().
//
// Why esp_timer instead of FreeRTOS xTimer: xTimer ticks at the FreeRTOS
// tick rate (1 ms on this build), which can't represent periods like
// 2500 us (400 Hz target). esp_timer is microsecond-resolution and
// backed by a 64-bit hardware timer — exact periodicity for any rate
// the chip can sustain.  Periods that ARE integer milliseconds (5000 us
// = 200 Hz, etc.) behave identically to the prior xTimer path; the
// switch is a no-op for the rates we tested before.
//
// Backlog handling
// ================
// Frame state is single-buffered. If the timer fires while the previous
// frame is still being drained, `pendingEmits_` accumulates; when the
// consumer finally gets back to loadNextFrameIfPending(), it observes
// the backlog and increments `missedEmits_` for each emission beyond
// the first. This mirrors a real UART overflow honestly: the consumer
// missed frames. `MissedEmits()` is exposed for `synth status` so we can
// detect the box failing to keep up at high rates.
//
// Wrapped in PerfScope(SynthBuild) so the (tiny — it's just a cursor
// reset to point at a pre-built byte array) per-frame overhead is
// attributable separately in `perf dump` and doesn't silently inflate
// efis_read / boom_read.

#pragma once

#ifdef ONSPEED_SYNTH_SENSORS

#include <Arduino.h>
#include <Stream.h>
#include <stddef.h>
#include <stdint.h>

#include <test_frames/SynthFrames.h>
#include <util/Perf.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <atomic>

class SyntheticStream : public Stream {
public:
    // Single-frame variant — for VN-300 and Boom. periodUs is the
    // emission period in microseconds.  timerName is shown by
    // esp_timer_dump() — pass a distinct name per instance so two
    // streams in the same build don't collide.
    SyntheticStream(const onspeed::test_frames::Frame& frame, uint32_t periodUs,
                    const char* timerName = "SynthEmit")
        : frames_(&frame),
          frameCount_(1),
          periodUs_(periodUs),
          timerName_(timerName),
          // Start at frameCount_-1 so the first emit's `(idx+1) % count`
          // wraps to 0 and serves frames in declared order.  Matters for
          // Skyview, which alternates !1 ADAHRS (frames[0]) and !3 EMS
          // (frames[1]).
          frameIdx_(frameCount_ - 1) {}

    // Multi-frame variant — for Skyview alternating !1 / !3.  Same
    // first-frame-zero invariant as the single-frame variant.
    SyntheticStream(const onspeed::test_frames::Frame* frames,
                    std::size_t frameCount, uint32_t periodUs,
                    const char* timerName = "SynthEmit")
        : frames_(frames),
          frameCount_(frameCount),
          periodUs_(periodUs),
          timerName_(timerName),
          frameIdx_(frameCount_ - 1) {}

    // Register the consumer task that should be woken (xTaskNotifyGive)
    // when the cadence timer fires. Must be called BEFORE Start(). The
    // task should `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` in its loop
    // — see EfisRead.cpp / BoomRead.cpp synth branches.
    void SetConsumerTask(TaskHandle_t consumer) { consumer_ = consumer; }

    // Arm the cadence timer. Returns false if the timer couldn't be
    // created (resource exhaustion). Must be called after SetConsumerTask
    // and after the consumer task is actually spawned.
    bool Start() {
        if (timer_ != nullptr) return true;
        const esp_timer_create_args_t args = {
            .callback        = &SyntheticStream::timerCallback,
            .arg             = this,
            // ESP_TIMER_TASK runs the callback on esp_timer's high-priority
            // dispatcher task, NOT in ISR context — safe to call xTaskNotifyGive
            // without the FromISR variant.
            .dispatch_method = ESP_TIMER_TASK,
            .name            = timerName_,
            // skip_unhandled_events controls light-sleep behaviour only —
            // whether a periodic timer that came due during sleep fires
            // once on wake or has all queued invocations replayed.  We
            // never call esp_light_sleep_start, so this flag is a no-op
            // for us; either value is fine.  Left at false to match the
            // IDF struct default.
            .skip_unhandled_events = false,
        };
        if (esp_timer_create(&args, &timer_) != ESP_OK) {
            timer_ = nullptr;
            return false;
        }
        const uint64_t period = (periodUs_ > 0) ? periodUs_ : 1000;
        if (esp_timer_start_periodic(timer_, period) != ESP_OK) {
            esp_timer_delete(timer_);
            timer_ = nullptr;
            return false;
        }
        return true;
    }

    int available() override {
        loadNextFrameIfPending();
        return static_cast<int>(bytesRemaining_);
    }

    int read() override {
        loadNextFrameIfPending();
        if (bytesRemaining_ == 0) return -1;
        const int b = frames_[frameIdx_].bytes[cursor_++];
        bytesRemaining_--;
        bytesEmitted_++;
        return b;
    }

    int peek() override {
        loadNextFrameIfPending();
        if (bytesRemaining_ == 0) return -1;
        return frames_[frameIdx_].bytes[cursor_];
    }

    void flush() override {}

    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }

    // For `synth status` console output.
    uint32_t FramesEmitted() const { return framesEmitted_; }
    uint64_t BytesEmitted()  const { return bytesEmitted_; }
    uint32_t MissedEmits()   const {
        return missedEmits_.load(std::memory_order_relaxed);
    }
    uint32_t PendingEmits()  const {
        return pendingEmits_.load(std::memory_order_relaxed);
    }

private:
    // Called from the esp_timer dispatcher task at periodUs_ cadence.
    // Increments the pending-emit count and notifies the consumer. Does
    // NOT touch the byte-state (cursor_, bytesRemaining_, frameIdx_) —
    // that's handled on the consumer task via loadNextFrameIfPending()
    // to keep all byte-state writes single-threaded.
    static void timerCallback(void* arg) {
        auto* self = static_cast<SyntheticStream*>(arg);
        self->pendingEmits_.fetch_add(1, std::memory_order_relaxed);
        if (self->consumer_ != nullptr) {
            xTaskNotifyGive(self->consumer_);
        }
    }

    // Called from the consumer task on available()/read()/peek(). If the
    // current frame is drained AND the timer has signalled at least one
    // emit since the last load, advance to the next frame. Any further
    // pending emits beyond the first are counted as missed (the consumer
    // can't keep up at this rate).
    void loadNextFrameIfPending() {
        if (bytesRemaining_ > 0) return;
        // Atomically take the pending-emits count down to zero. Anything
        // > 1 here means we missed (pendingEmits - 1) emits since the
        // previous frame load — that's an honest "couldn't keep up"
        // signal mirroring real UART overflow.
        const uint32_t pending =
            pendingEmits_.exchange(0, std::memory_order_acquire);
        if (pending == 0) return;
        if (pending > 1) {
            missedEmits_.fetch_add(pending - 1, std::memory_order_relaxed);
        }
        {
            onspeed::util::perf::PerfScope guard(
                onspeed::util::perf::ScopeId::SynthBuild);
            frameIdx_       = (frameIdx_ + 1) % frameCount_;
            cursor_         = 0;
            bytesRemaining_ = frames_[frameIdx_].len;
        }
        framesEmitted_++;
    }

    const onspeed::test_frames::Frame* frames_;
    std::size_t  frameCount_;
    uint32_t     periodUs_;
    const char*  timerName_;

    // Byte state — written only on the consumer task.  frameIdx_ is
    // initialised by the constructor to frameCount_-1; on the first
    // loadNextFrameIfPending() the (idx+1)%count step wraps to 0 so
    // frames[0] is served first.
    std::size_t  frameIdx_;
    std::size_t  cursor_         = 0;
    std::size_t  bytesRemaining_ = 0;
    uint32_t     framesEmitted_  = 0;
    uint64_t     bytesEmitted_   = 0;

    // Emit signaling — written from the timer service task, read
    // (and reset) on the consumer task. `pendingEmits_` saturates at
    // UINT32_MAX in pathological cases; not a real concern at any
    // bench rate we test.
    std::atomic<uint32_t> pendingEmits_{0};
    std::atomic<uint32_t> missedEmits_{0};

    esp_timer_handle_t timer_    = nullptr;
    TaskHandle_t       consumer_ = nullptr;
};

#endif  // ONSPEED_SYNTH_SENSORS
