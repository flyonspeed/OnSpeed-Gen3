// audio_harness.cpp — host harness around the firmware's full audio path.
//
// Reads per-tick body-angle state on stdin, emits int16 mono PCM @ 16 kHz
// on stdout.  Models every layer that Audio.cpp uses on the real box:
//
//   1. ToneCalc::calculateTone(aoa, thresholds)
//        → (EnToneType, fPulseFreq, fVolumeMult)
//   2. SetPulseFreq -> fTonePulseMaxSamples
//   3. SetTone:
//        if fTonePulseMaxSamples == 0 → MakeSolidSpec()
//        else                         → MakePulseSpec(pps, isStall, fromSolid)
//      Envelope.NoteOn(spec)
//   4. PlayTone:
//        per-channel scale = fVolume * fStallVolumeMult * channelGain
//        Mix(MixerInputs{ in=carrier, leftScale=fL, envelope=&env }, ...)
//      The DAHDR envelope provides the per-sample gate (silent delay,
//      attack ramp, hold, decay ramp, silent gap, optional sustain).
//
// stdin line:
//   "<aoa_deg> <ldmax_aoa> <os_fast_aoa> <os_slow_aoa> <stall_warn_aoa>"
//   " <lateral_g>"   (lateral G in g; positive = leftward per the
//                     wire convention from DisplaySerial.h)
//   "\n"
// One line per scenario tick (50 Hz nominal).
//
// stdout: raw int16 little-endian samples, **stereo interleaved**,
// 16000 Hz.
//
// Verbatim port of:
//   software/sketch_common/src/audio_io/Audio.cpp
// constants and structure.  Anything that diverges is a bug.

#include <audio/AudioMixer.h>
#include <audio/Envelope.h>
#include <audio/ToneCalc.h>
#include <audio/ToneSynth.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Verbatim from Audio.h / Audio.cpp.
static constexpr int   kSampleRateHz   = 16000;
static constexpr int   kToneBufferLen  = kSampleRateHz / 20;   // 50 ms
static constexpr int   kSamplesPerTick = kSampleRateHz / 50;   // 20 ms
static constexpr float kHighToneHz     = 1600.0f;
static constexpr float kLowToneHz      =  400.0f;
static constexpr float kToneRampMs     = 15.0f;
static constexpr float kStallRampMs    =  5.0f;
// 1000 / LOW_TONE_PPS_MAX / 2  =  1000 / 8.2 / 2  ≈  60.97 ms (Audio.cpp:118)
static constexpr float kSolidTransitionDelayMs =
    1000.0f / onspeed::LOW_TONE_PPS_MAX / 2.0f;

static float MsToSamples(float ms) { return ms * (kSampleRateHz / 1000.0f); }

// Verbatim port of Audio.cpp::MakePulseSpec.
static onspeed::audio::EnvelopeSpec MakePulseSpec(float pps, bool isStall, bool fromSolid)
{
    const float pulseDelayMs = 1000.0f / pps;
    const float toneLengthMs = pulseDelayMs - 3.0f;
    const float gapMs        = pulseDelayMs - toneLengthMs;
    const float rampMs       = isStall ? kStallRampMs : kToneRampMs;
    const float delayMs      = fromSolid
                                 ? kSolidTransitionDelayMs
                                 : (toneLengthMs * 0.5f);
    float       holdMs       = (toneLengthMs * 0.5f) - 2.0f * rampMs;
    if (holdMs < 0.0f) holdMs = 0.0f;

    onspeed::audio::EnvelopeSpec s;
    s.delaySamples   = MsToSamples(delayMs);
    s.attackSamples  = MsToSamples(rampMs);
    s.holdSamples    = MsToSamples(holdMs);
    s.decaySamples   = MsToSamples(rampMs);
    s.gapSamples     = MsToSamples(gapMs);
    s.releaseSamples = MsToSamples(rampMs);
    s.isSolid        = false;
    return s;
}

// Verbatim port of Audio.cpp::MakeSolidSpec.
static onspeed::audio::EnvelopeSpec MakeSolidSpec()
{
    onspeed::audio::EnvelopeSpec s;
    s.delaySamples   = MsToSamples(kSolidTransitionDelayMs);
    s.attackSamples  = MsToSamples(kToneRampMs);
    s.holdSamples    = 0.0f;
    s.decaySamples   = 0.0f;
    s.gapSamples     = 0.0f;
    s.releaseSamples = MsToSamples(kToneRampMs);
    s.isSolid        = true;
    return s;
}

int main(int /*argc*/, char** /*argv*/)
{
    std::vector<std::int16_t> tone_low(kToneBufferLen);
    std::vector<std::int16_t> tone_high(kToneBufferLen);
    onspeed::audio::SynthesizeLegacyCosine(
        kLowToneHz,  kSampleRateHz, tone_low.data(),  tone_low.size());
    onspeed::audio::SynthesizeLegacyCosine(
        kHighToneHz, kSampleRateHz, tone_high.data(), tone_high.size());

    onspeed::audio::Envelope    envelope;
    onspeed::audio::MixerState  mixer_state;

    // Track tone-engine state across ticks, exactly as Audio.cpp does.
    float                fTonePulseMaxSamples = 0.0f;
    float                fStallVolumeMult     = 1.0f;
    onspeed::EnToneType  enTone               = onspeed::EnToneType::None;
    onspeed::EnToneType  enLastEnvTone        = onspeed::EnToneType::None;

    // 3D-audio pan state.  Verbatim from Housekeeping.cpp:
    //   fChannelGain ← α·curve(|aLatCorr|)·sign(aLatCorr) + (1-α)·prevChannelGain
    //   left_pan_gain  = |-1 + channelGain|
    //   right_pan_gain = | 1 + channelGain|
    // Update cadence: every 100 ms (every 5th tick at 50 Hz).  α = 0.1.
    float fChannelGain = 0.0f;
    float fLeftPanGain  = 1.0f;
    float fRightPanGain = 1.0f;
    int   tick_index    = 0;
    constexpr float kPanAlpha = 0.1f;
    // AUDIO_3D_CURVE — proposed fix from issue #371.  The current
    // firmware curve `-92.822·x² + 20.025·x` collapses to zero above
    // 0.2157 g lateral, so spins (0.3–0.5 g) get NO pan.  Local
    // saturating-linear curve here so the demo videos audibly show
    // what the pan SHOULD do at realistic spin lateral G.
    //   curve(x) = min(1.0, 8.0 * |x|)  — saturates at 0.125 g, holds.
    auto pan_curve = [](float x) {
        const float v = 8.0f * x;
        return v > 1.0f ? 1.0f : v;
    };

    std::vector<std::int16_t> stereo_scratch(2 * 256);
    std::vector<std::int16_t> tick_buffer;
    tick_buffer.reserve(2 * kSamplesPerTick);   // stereo: 2 samples per frame

    char line[256];
    while (std::fgets(line, sizeof(line), stdin))
    {
        float aoa = 0.0f, ldmax = 0.0f, os_fast = 0.0f, os_slow = 0.0f, stall_warn = 0.0f;
        float lateral_g = 0.0f;
        const int got = std::sscanf(line, "%f %f %f %f %f %f",
                                    &aoa, &ldmax, &os_fast, &os_slow, &stall_warn,
                                    &lateral_g);
        if (got < 5) {
            std::fprintf(stderr, "audio_harness: bad input line: %s", line);
            return 2;
        }
        // got == 5 (no lateral_g) is treated as lateral_g = 0 — backward-compat
        // with the original 5-field format.

        // 3D-pan update at 10 Hz (every 5th 50 Hz tick).
        if (tick_index % 5 == 0) {
            const float lat_sign = (lateral_g >= 0.0f) ? 1.0f : -1.0f;
            float curve_gain = pan_curve(std::fabs(lateral_g));
            if (curve_gain > 1.0f) curve_gain = 1.0f;
            if (curve_gain < 0.0f) curve_gain = 0.0f;
            curve_gain *= lat_sign;
            fChannelGain = kPanAlpha * curve_gain + (1.0f - kPanAlpha) * fChannelGain;
            if (fChannelGain >  1.0f) fChannelGain =  1.0f;
            if (fChannelGain < -1.0f) fChannelGain = -1.0f;
            fLeftPanGain  = std::fabs(-1.0f + fChannelGain);
            fRightPanGain = std::fabs( 1.0f + fChannelGain);
        }
        ++tick_index;

        // 1. ToneCalc.
        const onspeed::ToneThresholds th{ldmax, os_fast, os_slow, stall_warn};
        const onspeed::ToneResult     r = onspeed::calculateTone(aoa, th);

        fStallVolumeMult = r.fVolumeMult;

        // 2. SetPulseFreq (Audio.cpp::SetPulseFreq).
        if (r.fPulseFreq < 1.0f || r.fPulseFreq > 25.0f) {
            fTonePulseMaxSamples = 0.0f;
        } else {
            fTonePulseMaxSamples = kSampleRateHz / (r.fPulseFreq * 2.0f);
        }

        // 3. SetTone (Audio.cpp::SetTone).
        if (r.enTone == onspeed::EnToneType::None) {
            envelope.NoteOff();
            enTone = onspeed::EnToneType::None;
        } else {
            onspeed::audio::EnvelopeSpec spec;
            if (fTonePulseMaxSamples == 0.0f) {
                spec = MakeSolidSpec();
            } else {
                const float pps      = kSampleRateHz / (fTonePulseMaxSamples * 2.0f);
                const bool  isStall  = (pps >= onspeed::HIGH_TONE_STALL_PPS - 0.5f);
                const bool  fromSolid = envelope.IsCurrentSolid();
                spec = MakePulseSpec(pps, isStall, fromSolid);
            }
            envelope.NoteOn(spec);
            enLastEnvTone = r.enTone;
            enTone        = r.enTone;
        }

        // 4. PlayTone (Audio.cpp::PlayTone).  Pump 320 samples (one
        //    tick's worth) through the mixer, in <=256-frame chunks so
        //    the mixer's pulse-state stays stable across boundaries.
        //
        // Per-channel scale composes (Audio.cpp:640-642):
        //   sample × fStallVolumeMult × fVolume × fLeftGain
        //   ──────  ────────────────  ───────  ─────────
        //   carrier  per-PPS volume   master    3D pan
        //
        // Master volume = 1.0 (pot at top); 3D-pan gains come from the
        // 10 Hz update above.  Hard-clamp at 1.0 per Audio.cpp:663-664
        // to prevent clipping when 3D pan ≥ 1.0 combines with master.
        const float fVolume = 1.0f;
        float fL = fVolume * fStallVolumeMult * fLeftPanGain;
        float fR = fVolume * fStallVolumeMult * fRightPanGain;
        if (fL > 1.0f) fL = 1.0f;
        if (fR > 1.0f) fR = 1.0f;

        // Source carrier for this pump.  During Release the firmware
        // keeps pumping the *previous* tone's source (s_LastEnvTone) so
        // the envelope's release tail decays on a continuous waveform.
        // Mirror that here.
        onspeed::EnToneType effective =
            (enTone != onspeed::EnToneType::None) ? enTone : enLastEnvTone;

        tick_buffer.clear();

        if (effective == onspeed::EnToneType::None && envelope.IsIdle()) {
            // Truly silent — emit kSamplesPerTick stereo frames of zeros.
            tick_buffer.assign(2 * kSamplesPerTick, 0);
        } else {
            const std::int16_t* carrier =
                (effective == onspeed::EnToneType::Low)
                    ? tone_low.data()
                    : tone_high.data();
            // Persistent carrier index for phase continuity.
            static std::int64_t carrier_idx = 0;

            std::int64_t emitted = 0;
            while (emitted < kSamplesPerTick) {
                const std::int64_t chunk = std::min<std::int64_t>(
                    256, kSamplesPerTick - emitted);

                std::vector<std::int16_t> carrier_chunk(chunk);
                for (std::int64_t k = 0; k < chunk; ++k) {
                    carrier_chunk[k] = carrier[(carrier_idx + k) % kToneBufferLen];
                }
                carrier_idx += chunk;

                onspeed::audio::MixerInputs inp;
                inp.in         = carrier_chunk.data();
                inp.leftScale  = fL;
                inp.rightScale = fR;
                inp.envelope   = &envelope;

                if (static_cast<std::int64_t>(stereo_scratch.size()) < 2 * chunk)
                    stereo_scratch.resize(2 * chunk);

                onspeed::audio::Mix(inp, stereo_scratch.data(), chunk, mixer_state);

                // Mixer wrote interleaved L,R,L,R,... — copy as-is.
                for (std::int64_t k = 0; k < 2 * chunk; ++k) {
                    tick_buffer.push_back(stereo_scratch[k]);
                }
                emitted += chunk;
            }
        }

        std::fwrite(tick_buffer.data(),
                    sizeof(std::int16_t), tick_buffer.size(), stdout);
    }

    std::fflush(stdout);
    return 0;
}
