
#include "src/Globals.h"
#include "src/config/Config.h"
#include "src/drivers/Mcp3202Adc.h"
#include <sensors/FlapsDetector.h>
#include "src/ahrs/FlapSnapshot.h"

// Lock-free snapshot of flap state. Owned here; published by both
// Flaps::Update overloads and by HandleConfigSave's flap-vector swap.
// See src/ahrs/FlapSnapshot.h for the payload and the writer contract.
onspeed::util::SnapshotPublisher<onspeed::ahrs::FlapSnapshotPayload>
    onspeed::ahrs::g_FlapSnapshot;

// ----------------------------------------------------------------------------

Flaps::Flaps()
{
    if constexpr (!kHasExternalMcp3202)
        pinMode(kPinFlap, INPUT_PULLUP);
}

// ----------------------------------------------------------------------------

// Read the analog flap position from the MCP3202 (or built-in ADC on legacy
// boards).  Takes xSensorMutex around the SPI transaction to share the bus
// with the IMU and pressure sensors; held only for the few microseconds the
// SPI transfer takes.
//
// Putting the take here (rather than at the call site) keeps the lock graph
// simple: every caller -- SensorIO, ConsoleSerial, LogReplay -- gets bus
// safety without nesting xSensorMutex around any code that subsequently
// takes xAhrsMutex.

uint16_t Flaps::Read()
{
    if constexpr (kHasExternalMcp3202)
    {
        uint16_t uVal = 0;
        xSemaphoreTake(xSensorMutex, portMAX_DELAY);
        uVal = Mcp3202Read(kAdcChFlap);
        xSemaphoreGive(xSensorMutex);
        return uVal;
    }
    else
        return analogRead(kPinFlap);
}

// ----------------------------------------------------------------------------

// Build and publish the flap snapshot payload from the live globals plus the
// index/position just written. The caller MUST hold xAhrsMutex (this reads
// g_Config.aFlaps), which also serializes this publish against the other two
// writers (the manual-override overload and HandleConfigSave's swap) so the
// publisher's single-writer invariant holds.
static void PublishFlapSnapshot_(int iIdx, int iPos, uint16_t uVal)
{
    onspeed::ahrs::FlapSnapshotPayload p;
    const size_t nFlaps = g_Config.aFlaps.size();
    const size_t nCopy  = (nFlaps < (size_t)onspeed::MAX_AOA_CURVES)
                            ? nFlaps : (size_t)onspeed::MAX_AOA_CURVES;
    for (size_t i = 0; i < nCopy; ++i)
        p.aFlaps[i] = g_Config.aFlaps[i];
    p.nFlaps    = (uint8_t)nCopy;
    p.iIndex    = iIdx;
    p.iPosition = iPos;
    p.uValue    = uVal;
    p.bValid    = (nCopy > 0) && (iIdx >= 0) && ((size_t)iIdx < nCopy);
    onspeed::ahrs::g_FlapSnapshot.publish(p);
}

// Read the flap position and update iIndex / iPosition.
//
// The xAhrsMutex window covers the entire detect-and-write sequence so
// HandleConfigSave's flap-vector swap on Core 0 cannot retire `aFlaps`
// between the size snapshot and the index write.  Without this window the
// detector could index a flap entry from the old vector against a size
// from the new one (TOCTOU on `aFlaps.size()`).

void Flaps::Update()
{
    // Read the analog value first; the SPI take is independent of xAhrsMutex.
    uValue = Read();

    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        // Skip this 1 Hz tick; iIndex / iPosition stay at their last
        // known values rather than getting written from a torn snapshot.
        return;
    }

    const size_t nFlaps = g_Config.aFlaps.size();
    if (nFlaps == 0u)
    {
        iPosition = -1;
        PublishFlapSnapshot_(iIndex, iPosition, uValue);
        xSemaphoreGive(xAhrsMutex);
        return;
    }

    uint16_t potPositions[MAX_AOA_CURVES];
    const size_t nUsed = (nFlaps < (size_t)MAX_AOA_CURVES) ? nFlaps : (size_t)MAX_AOA_CURVES;
    for (size_t i = 0u; i < nUsed; ++i)
        potPositions[i] = static_cast<uint16_t>(g_Config.aFlaps[i].iPotPosition);

    // Delegate the midpoint-threshold detection to the platform-independent
    // core function. It handles both ascending and descending wiring.
    onspeed::FlapState state = onspeed::sensors::DetectFlaps(uValue, potPositions, nUsed);

    // Resolve directly from the same snapshot the detector saw -- never
    // re-read aFlaps.size() after the detection call.
    const int iSafeIdx = constrain(state.detectedIndex, 0, (int)nUsed - 1);
    iIndex    = iSafeIdx;
    iPosition = g_Config.aFlaps[iSafeIdx].iDegrees;

    PublishFlapSnapshot_(iIndex, iPosition, uValue);
    xSemaphoreGive(xAhrsMutex);
}

// ----------------------------------------------------------------------------

// Sometimes (like Test Pot mode) you just want to set an index.  Same
// xAhrsMutex window as the auto-detect overload, for the same reason.

void Flaps::Update(int iFlapsIndex)
{
    if (xSemaphoreTake(xAhrsMutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return;

    if (g_Config.aFlaps.empty())
    {
        iIndex    = 0;
        iPosition = -1;
    }
    else
    {
        iIndex    = constrain(iFlapsIndex, 0, (int)g_Config.aFlaps.size() - 1);
        iPosition = g_Config.aFlaps[iIndex].iDegrees;
    }

    PublishFlapSnapshot_(iIndex, iPosition, uValue);
    xSemaphoreGive(xAhrsMutex);
}

