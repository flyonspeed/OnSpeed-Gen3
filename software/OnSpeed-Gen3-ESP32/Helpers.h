// HelperFunctions.ino

#ifndef _HELPER_H_
#define _HELPER_H_

#include "Globals.h"

#define MIN(a,b)    (a < b ? a : b)
#define MAX(a,b)    (a > b ? a : b)

float       mapfloat(float x, float in_min, float in_max, float out_min, float out_max);
void        _softRestart();
uint32_t    freeMemory();

#endif