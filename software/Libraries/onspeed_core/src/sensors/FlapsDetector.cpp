// FlapsDetector.cpp — ADC reading -> flap index via midpoint thresholds
//
// Ported from Flaps::Update() in the sketch. Algorithm is unchanged;
// see the header for the spec and the test suite for characterization.

#include <sensors/FlapsDetector.h>
#include <algorithm>

namespace onspeed::sensors {

onspeed::FlapState DetectFlaps(
    uint16_t rawAdc,
    const uint16_t* positions,
    size_t positionCount)
{
    onspeed::FlapState out;
    out.rawAdc = rawAdc;

    if (positionCount == 0u)
    {
        // No positions configured; signal caller with invalid state.
        out.valid = false;
        return out;
    }

    out.detectedIndex = 0;
    out.valid = true;

    if (positionCount == 1u)
    {
        // Only one position defined; index is always 0.
        out.normalized = 0.0f;
        return out;
    }

    // Determine wiring order from the configured endpoints.
    // If positions[0] > positions[N-1], the pot is wired descending
    // (higher counts correspond to lower flap angle / flaps-up).
    const bool descending = (positions[0] > positions[positionCount - 1u]);

    // Walk the position array and compare against midpoint thresholds.
    // This is a verbatim port of the Flaps::Update() loop; bisectability
    // is preserved by keeping the logic identical.
    for (size_t i = 1u; i < positionCount; ++i)
    {
        const int midpoint = (static_cast<int>(positions[i])
                              + static_cast<int>(positions[i - 1u])) / 2;

        if (!descending)
        {
            if (static_cast<int>(rawAdc) > midpoint)
                out.detectedIndex = static_cast<int>(i);
        }
        else
        {
            if (static_cast<int>(rawAdc) < midpoint)
                out.detectedIndex = static_cast<int>(i);
        }
    }

    // Normalized position: 0.0 = first configured position (flaps up),
    // 1.0 = last configured position (full flaps).
    const float span = descending
        ? static_cast<float>(positions[0]) - static_cast<float>(positions[positionCount - 1u])
        : static_cast<float>(positions[positionCount - 1u]) - static_cast<float>(positions[0]);

    if (span > 0.0f)
    {
        const float raw = descending
            ? static_cast<float>(positions[0]) - static_cast<float>(rawAdc)
            : static_cast<float>(rawAdc) - static_cast<float>(positions[0]);
        out.normalized = std::clamp(raw / span, 0.0f, 1.0f);
    }
    else
    {
        // Degenerate: all positions at the same ADC count.
        out.normalized = 0.0f;
    }

    return out;
}

}   // namespace onspeed::sensors
