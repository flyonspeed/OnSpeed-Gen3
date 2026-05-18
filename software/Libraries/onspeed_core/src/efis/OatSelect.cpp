// OatSelect.cpp
//
// See OatSelect.h for the decision rule.

#include <efis/OatSelect.h>

#include <cmath>

namespace onspeed::efis {

float SelectDisplayOatC(bool  calSourceEfis,
                        bool  readEfisDataEnabled,
                        bool  efisIsFresh,
                        bool  oatSensorEnabled,
                        float efisOatC,
                        float internalOatC)
{
    if (calSourceEfis && readEfisDataEnabled && efisIsFresh
        && std::isfinite(efisOatC))
    {
        return efisOatC;
    }
    if (oatSensorEnabled && std::isfinite(internalOatC))
    {
        return internalOatC;
    }
    return 0.0f;
}

}   // namespace onspeed::efis
