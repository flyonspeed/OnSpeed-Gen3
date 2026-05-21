// SyntheticStream — Arduino Stream that emits pre-built byte frames on a
// fixed cadence.
//
// Perf-build-only (-DONSPEED_SYNTH_SENSORS=1). Constructed with one or more
// Frames (from onspeed_core/test_frames/SynthFrames.h). The Stream rotates
// through the frame array on each emission window. Skyview passes 2 frames
// (!1 ADAHRS, !3 EMS alternating). VN-300 and Boom pass 1.
//
// Cadence: ensureFrameReady() rebuilds the cursor when the previous frame
// is drained AND millis() has reached the next-emission deadline. Between
// frames, available() returns 0 — same shape as a quiet UART — so the
// upstream parser's drain loop exits naturally.
//
// Wrapped in PerfScope(SynthBuild) so the (tiny — it's just a memcpy +
// cursor reset) per-frame overhead is attributable separately in `perf
// dump` and doesn't silently inflate efis_read / boom_read.

#pragma once

#ifdef ONSPEED_SYNTH_SENSORS

#include <Arduino.h>
#include <Stream.h>
#include <stddef.h>
#include <stdint.h>

#include <test_frames/SynthFrames.h>
#include <util/Perf.h>

class SyntheticStream : public Stream {
public:
    // Single-frame variant — for VN-300 and Boom.
    SyntheticStream(const onspeed::test_frames::Frame& frame, uint32_t periodMs)
        : frames_(&frame),
          frameCount_(1),
          periodMs_(periodMs),
          nextEmitMs_(0),
          frameIdx_(0),
          cursor_(0),
          bytesRemaining_(0),
          framesEmitted_(0),
          bytesEmitted_(0) {}

    // Multi-frame variant — for Skyview alternating !1 / !3.
    SyntheticStream(const onspeed::test_frames::Frame* frames,
                    std::size_t frameCount, uint32_t periodMs)
        : frames_(frames),
          frameCount_(frameCount),
          periodMs_(periodMs),
          nextEmitMs_(0),
          frameIdx_(0),
          cursor_(0),
          bytesRemaining_(0),
          framesEmitted_(0),
          bytesEmitted_(0) {}

    int available() override {
        ensureFrameReady();
        return static_cast<int>(bytesRemaining_);
    }

    int read() override {
        ensureFrameReady();
        if (bytesRemaining_ == 0) return -1;
        const int b = frames_[frameIdx_].bytes[cursor_++];
        bytesRemaining_--;
        bytesEmitted_++;
        return b;
    }

    int peek() override {
        ensureFrameReady();
        if (bytesRemaining_ == 0) return -1;
        return frames_[frameIdx_].bytes[cursor_];
    }

    void flush() override {}

    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }

    // For `synth status` console output.
    uint32_t FramesEmitted() const { return framesEmitted_; }
    uint64_t BytesEmitted()  const { return bytesEmitted_; }

private:
    void ensureFrameReady() {
        if (bytesRemaining_ > 0) return;            // current frame not drained
        const uint32_t now = millis();
        if (nextEmitMs_ != 0 && now < nextEmitMs_) return;

        {
            onspeed::util::perf::PerfScope guard(
                onspeed::util::perf::ScopeId::SynthBuild);
            // Rotate to next frame, reset cursor. Bytes are pre-built —
            // this scope captures only the cursor-reset cost (≈ a few
            // ns), so synth-build will read as essentially-zero in the
            // perf report. That's correct: with fixed frames there's no
            // construction cost to amortise.
            frameIdx_       = (frameIdx_ + 1) % frameCount_;
            cursor_         = 0;
            bytesRemaining_ = frames_[frameIdx_].len;
        }
        framesEmitted_++;

        // Anchor next emission. If we fell badly behind (e.g. perf paused),
        // re-anchor to now rather than try to catch up.
        const uint32_t target = nextEmitMs_ + periodMs_;
        nextEmitMs_ = (now > target + 2 * periodMs_) ? (now + periodMs_) : target;
    }

    const onspeed::test_frames::Frame* frames_;
    std::size_t  frameCount_;
    uint32_t     periodMs_;
    uint32_t     nextEmitMs_;
    std::size_t  frameIdx_;
    std::size_t  cursor_;
    std::size_t  bytesRemaining_;
    uint32_t     framesEmitted_;
    uint64_t     bytesEmitted_;
};

#endif  // ONSPEED_SYNTH_SENSORS
