// Panning.cpp — 3D audio left/right gain calculation from lateral G.
// See Panning.h for pipeline notes.

#include <audio/Panning.h>

#include <cmath>

namespace onspeed {
namespace audio {

namespace {

// Quadratic gain curve (no true dead-zone — f'(0) = 20.025).  Tuned
// in Gen2 against the inclinometer; |lateralG| ≈ 0.08 corresponds to
// one slip-skid ball width.
//   f(0.00)  =  0.000
//   f(0.05)  =  0.769
//   f(0.08)  =  1.008  → caller clamps to 1.0
//   f(0.108) =  1.080  (curve apex)
//   f(0.216) =  0.000  (descending root — caller clamps the negative
//                       tail to 0, so very large |lateralG| renders
//                       as centered audio)
inline float CurveGain(float absLateralG)
{
    return -92.822f * absLateralG * absLateralG + 20.025f * absLateralG;
}

inline float Clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

PanResult Apply3DPan(float lateralG, PanState& state, const PanConfig& cfg)
{
    const float fAbs    = std::fabs(lateralG);
    const float fSign   = (lateralG >= 0.0f) ? 1.0f : -1.0f;

    float fCurve = CurveGain(fAbs);
    fCurve = Clamp(fCurve, 0.0f, 1.0f);
    fCurve *= fSign;

    state.channelGain = cfg.smoothingFactor * fCurve
                      + (1.0f - cfg.smoothingFactor) * state.channelGain;
    state.channelGain = Clamp(state.channelGain, -1.0f, 1.0f);

    float fLeft  = std::fabs(-1.0f + state.channelGain);
    float fRight = std::fabs( 1.0f + state.channelGain);

    // Max-gain normalization.  Preserves L/R ratio (panning direction)
    // without ever exceeding 1.0 on either channel.
    const float fMax = (fLeft > fRight) ? fLeft : fRight;
    if (fMax > 1.0f)
    {
        fLeft  /= fMax;
        fRight /= fMax;
    }

    PanResult out;
    out.leftGain  = fLeft;
    out.rightGain = fRight;
    return out;
}

}  // namespace audio
}  // namespace onspeed
