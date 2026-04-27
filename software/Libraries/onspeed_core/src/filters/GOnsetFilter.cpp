// GOnsetFilter.cpp - implementation. See header for design notes.

#include <filters/GOnsetFilter.h>

#include <cmath>

namespace onspeed {

namespace {

inline bool IsFiniteFloat(float v) {
    return !std::isnan(v) && !std::isinf(v);
}

} // namespace

GOnsetFilter::GOnsetFilter(float timeConstantSec)
    : tauSec_(timeConstantSec > 0.0f ? timeConstantSec : 0.25f)
    , prevG_(0.0f)
    , smoothed_(0.0f)
    , hasPrev_(false)
{
}

float GOnsetFilter::Update(float verticalG, float dtSec)
{
    // Reject malformed inputs; output and state are unchanged.
    if (!IsFiniteFloat(verticalG) || !(dtSec > 0.0f)) {
        return smoothed_;
    }

    // First valid sample: seed prev, no derivative yet.
    if (!hasPrev_) {
        prevG_ = verticalG;
        hasPrev_ = true;
        smoothed_ = 0.0f;
        return smoothed_;
    }

    const float rawRate = (verticalG - prevG_) / dtSec;
    prevG_ = verticalG;

    // EMA blend with alpha = dt / (tau + dt). For dt << tau, alpha is small
    // (heavy smoothing); as dt approaches tau, alpha approaches 0.5.
    const float alpha = dtSec / (tauSec_ + dtSec);
    smoothed_ = alpha * rawRate + (1.0f - alpha) * smoothed_;

    return smoothed_;
}

void GOnsetFilter::Reset()
{
    prevG_    = 0.0f;
    smoothed_ = 0.0f;
    hasPrev_  = false;
}

void GOnsetFilter::SetTimeConstant(float timeConstantSec)
{
    if (timeConstantSec > 0.0f) {
        tauSec_ = timeConstantSec;
    }
}

} // namespace onspeed
