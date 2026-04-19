// BoomConvert.cpp - Boom probe polynomial calibration conversions

#include "BoomConvert.h"

namespace onspeed {

float BoomAlphaConvert(int counts)
{
    float f = (float)counts;
    return 7.0918e-13f*f*f*f*f - 1.1698e-8f*f*f*f + 7.0109e-5f*f*f - 0.21624f*f + 310.21f;
}

float BoomBetaConvert(int counts)
{
    float f = (float)counts;
    return 2.0096e-13f*f*f*f*f - 3.7124e-9f*f*f*f + 2.5497e-5f*f*f - 3.7141e-2f*f - 72.505f;
}

float BoomStaticConvert(int counts)
{
    return 0.00012207f * (counts - 1638) * 1000.0f;
}

float BoomDynamicConvert(int counts)
{
    return (0.01525902f * (counts - 1638)) - 100.0f;
}

} // namespace onspeed
