// Panning.h — 3D audio left/right gain calculation from lateral G.
//
// Maps a single lateral acceleration sample (the slip-skid ball signal,
// in G) into a pair of channel gains (left, right) suitable for passing
// to AudioPlay::SetGain().
//
// Pipeline per call:
//
//   1. Curve gain: a quadratic ramps |lateralG| through unity at
//      ±0.08 G (one slip-skid ball width in coordinated-flight
//      terms) and clamps to [0, 1].  Beyond ~0.216 G the quadratic
//      goes negative; the clamp returns it to 0, so very large
//      lateral G decays the pan back to centered.
//
//   2. Sign restore: the curve operates on |lateralG|; the sign is
//      reattached so positive lateralG steers the audio to the right
//      channel (matching the inclinometer convention).
//
//   3. Smoothing: a single-pole IIR pulls the persistent channelGain
//      state toward the new curve gain at `cfg.smoothingFactor` per
//      tick.  Caller drives ticks at a fixed cadence (Housekeeping
//      runs at 10 Hz).
//
//   4. L/R split: the smoothed channelGain ∈ [-1, 1] is split into
//      raw per-channel gains
//          left  = | -1 + channelGain |
//          right = | +1 + channelGain |
//      The raw gains are in [0, 2] — centered = 1.0/1.0, hard pan
//      = 0.0/2.0.
//
//   5. Max-gain normalization: if the louder channel exceeds 1.0,
//      both channels are divided by the louder side.  This preserves
//      the panning *ratio* (audible direction) while keeping each
//      channel ≤ 1.0, so the downstream output stage never has to
//      hard-clip pan and squash directional cues at high master
//      volume.  Centered stays 1.0/1.0; hard pan becomes 0.0/1.0.
//
// State (`PanState`) is caller-owned: a single `channelGain` float
// holds the smoothed pan position across calls.  Construct as zero
// for "centered" and reuse the same instance every tick.

#ifndef ONSPEED_CORE_AUDIO_PANNING_H
#define ONSPEED_CORE_AUDIO_PANNING_H

namespace onspeed {
namespace audio {

struct PanConfig {
    // Single-pole IIR smoothing.  channelGain[t] = a*curveGain + (1-a)*channelGain[t-1].
    // Caller drives ticks at a fixed cadence; smoothingFactor determines
    // how fast the panning responds to a step in lateralG.
    float smoothingFactor = 0.1f;
};

struct PanState {
    // Smoothed pan position in [-1, 1].  Zero is centered.
    float channelGain = 0.0f;
};

struct PanResult {
    // Per-channel gains in [0, 1].  Centered is (1.0, 1.0); hard pan
    // is (0.0, 1.0) or (1.0, 0.0).
    float leftGain  = 1.0f;
    float rightGain = 1.0f;
};

// Map a lateral-G sample to normalized stereo channel gains.  Updates
// `state.channelGain` (persisted across calls for smoothing).
PanResult Apply3DPan(float lateralG, PanState& state, const PanConfig& cfg);

}  // namespace audio
}  // namespace onspeed

#endif  // ONSPEED_CORE_AUDIO_PANNING_H
