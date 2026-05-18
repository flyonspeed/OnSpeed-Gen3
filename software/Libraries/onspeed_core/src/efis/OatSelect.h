// OatSelect.h
//
// Picks the OAT (°C) to report on user-visible display surfaces — the
// WebSocket JSON broadcast (`DataServer.cpp`) and the `#1` display
// serial frame to the M5 (`DisplaySerial.cpp`).
//
// Decision rule:
//
//   1. If `calSourceEfis && readEfisDataEnabled && efisIsFresh` and the
//      EFIS-supplied OAT is finite, use the EFIS value.
//   2. Else if `oatSensorEnabled`, use the internal sensor value.
//   3. Else 0.0f.
//
// `efisIsFresh` is a sketch-side, pre-computed boolean (the sketch
// calls `EfisSerial::IsDataFresh(2000)`, gated on a recently decoded
// EFIS frame).  Staleness logic is not in `onspeed_core` because it
// depends on Arduino's `millis()`; the helper takes the result.
//
// This mirrors the OAT-source gates the AHRS TAS-input selector uses
// (see `onspeed_core/ahrs/Ahrs.cpp`).  Display surfaces and the AHRS
// share the same contract: prefer the EFIS only when the calibration
// source is EFIS, the EFIS feed is enabled, and a frame has been
// decoded recently; otherwise fall back to the internal DS18B20 sensor
// when configured.

#ifndef ONSPEED_CORE_EFIS_OAT_SELECT_H
#define ONSPEED_CORE_EFIS_OAT_SELECT_H

namespace onspeed::efis {

float SelectDisplayOatC(bool  calSourceEfis,
                        bool  readEfisDataEnabled,
                        bool  efisIsFresh,
                        bool  oatSensorEnabled,
                        float efisOatC,
                        float internalOatC);

}   // namespace onspeed::efis

#endif   // ONSPEED_CORE_EFIS_OAT_SELECT_H
