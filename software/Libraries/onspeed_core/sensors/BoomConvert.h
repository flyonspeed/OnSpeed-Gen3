// BoomConvert.h - Boom probe polynomial calibration conversions
//
// Converts raw ADC counts from a boom probe into physical units
// using probe-specific polynomial calibration curves.

#pragma once

namespace onspeed {

/// Convert boom alpha raw counts to degrees
/// @param counts Raw ADC count from boom alpha channel
/// @return Alpha angle in degrees
float BoomAlphaConvert(int counts);

/// Convert boom beta raw counts to degrees
/// @param counts Raw ADC count from boom beta channel
/// @return Beta angle in degrees
float BoomBetaConvert(int counts);

/// Convert boom static pressure raw counts to millibars
/// @param counts Raw ADC count from boom static channel
/// @return Static pressure in millibars
float BoomStaticConvert(int counts);

/// Convert boom dynamic pressure raw counts to millibars
/// @param counts Raw ADC count from boom dynamic channel
/// @return Dynamic pressure in millibars
float BoomDynamicConvert(int counts);

} // namespace onspeed
