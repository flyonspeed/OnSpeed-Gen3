// HelperFunctions.ino

#ifndef _HELPER_H_
#define _HELPER_H_

#include <cstdint>
#include <OnSpeedTypes.h>

using onspeed::mapfloat;

#define MIN(a,b)    (a < b ? a : b)
#define MAX(a,b)    (a > b ? a : b)

void        _softRestart();
uint32_t    freeMemory();

#endif