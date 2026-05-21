// SynthFrames — fixed valid wire frames for the perf-synth firmware build
// and for native unit tests.
//
// Each protocol exposes a (or, for Skyview, a pair of) const byte arrays
// constructed once at first use. The bytes are deterministic and minimal
// — they're a fixed valid frame representative of cruise. Field values
// don't vary across calls; parser cost is the same whether they do or
// don't (CRC + memcpy + applyXxx copies are O(frame size), not O(value)),
// and fixed bytes are far easier to verify against the parser spec.
//
// Caller pattern:
//
//   const auto& f = onspeed::test_frames::Vn300Frame();
//   uart.write(f.bytes, f.len);
//
// For Skyview, alternation between !1 (ADAHRS) and !3 (EMS) is handled
// by SyntheticStream rotating through SkyviewFrames() — the array has
// two entries.
//
// Why no virtual interface: each accessor returns a flat byte buffer.
// SyntheticStream cycles through one or more such buffers on a fixed
// cadence. That's enough plumbing — adding a vtable would just hide
// it. If a future "degraded wire" mode needs varying bytes, we can
// add a separate path then.

#ifndef ONSPEED_CORE_TEST_FRAMES_SYNTH_FRAMES_H
#define ONSPEED_CORE_TEST_FRAMES_SYNTH_FRAMES_H

#include <cstddef>
#include <cstdint>

namespace onspeed::test_frames {

struct Frame {
    const uint8_t* bytes;
    std::size_t    len;
};

// 127-B VectorNav VN-300 frame. 50 Hz native rate.
const Frame& Vn300Frame();
constexpr uint32_t kVn300PeriodMs = 20;

// Dynon SkyView !1 ADAHRS (74 B) + !3 EMS (225 B), alternating.
// Real SkyView alternates the two; SyntheticStream cycles through both.
// Returns a pointer to a 2-element array.
const Frame* SkyviewFrames();
constexpr std::size_t kSkyviewFrameCount = 2;
constexpr uint32_t   kSkyviewPeriodMs   = 50;   // ~10 Hz combined

// Boom probe $AIRDAQ,deviceId,tag,N,N,N,N,XX\r\n line. 50 Hz native.
// Format mirrors a live AirDAQ boom; see SynthFrames.cpp::BuildBoom.
const Frame& BoomFrame();
constexpr uint32_t kBoomPeriodMs = 20;

}   // namespace onspeed::test_frames

#endif
