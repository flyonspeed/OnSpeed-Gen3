// FlapsDetector.h — ADC reading -> flap index via midpoint thresholds
//
// The pilot-configured flap lever is sensed via a pot wiper on an ADC
// channel. Each configured flap position has a nominal ADC value; the
// detected position is the one whose nominal value is closest to the
// current reading, determined by midpoint threshold crossings.
//
// The detection algorithm supports both ascending and descending pot
// wiring (first position > last position triggers descending mode).
// This matches the behavior implemented in Flaps::Update() in the sketch.

#ifndef ONSPEED_CORE_SENSORS_FLAPS_DETECTOR_H
#define ONSPEED_CORE_SENSORS_FLAPS_DETECTOR_H

#include <cstddef>
#include <cstdint>
#include <types/FlapState.h>

namespace onspeed::sensors {

// Detect the current flap index from a raw ADC reading and the array of
// configured nominal pot positions.
//
// Algorithm:
//   1. If positionCount == 0, return invalid FlapState.
//   2. If positionCount == 1, return index 0 (only one position defined).
//   3. Determine wiring order: if positions[0] > positions[N-1], the pot is
//      wired in descending order (higher counts = flaps up).
//   4. Walk positions[1..N-1] computing midpoints between adjacent entries.
//      In ascending order: if rawAdc > midpoint, advance detectedIndex.
//      In descending order: if rawAdc < midpoint, advance detectedIndex.
//
// The normalized field is clamped to [0, 1]:
//   ascending:  (rawAdc - positions[0]) / (positions[N-1] - positions[0])
//   descending: (positions[0] - rawAdc) / (positions[0] - positions[N-1])
//
// `positions` is a pointer to positionCount uint16_t values.
// `positionCount` is the number of configured flap positions (typically 2–4).
onspeed::FlapState DetectFlaps(
    uint16_t rawAdc,
    const uint16_t* positions,
    size_t positionCount);

}   // namespace onspeed::sensors

#endif  // ONSPEED_CORE_SENSORS_FLAPS_DETECTOR_H
