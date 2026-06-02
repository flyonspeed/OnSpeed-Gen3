
#pragma once

class Flaps
{
public:
    Flaps();

    // Data
public:
    uint16_t    uValue;     // Analog flaps value
    int         iIndex;
    int         iPosition;  // Flap position in degrees from lookup table

    // Methods
public:
    uint16_t    Read();
    void        Update();
    void        Update(int iFlapsIndex);

    // Publish the current flap state (this object's iIndex/iPosition/uValue
    // plus g_Config.aFlaps) to the lock-free g_FlapSnapshot. Both Update
    // overloads call this internally under xAhrsMutex. The replay path
    // (LogReplayTask, sole flap-state writer in replay mode) calls it
    // directly after writing the members, mirroring AHRS::PublishSnapshot().
    void        PublishSnapshot();

};
