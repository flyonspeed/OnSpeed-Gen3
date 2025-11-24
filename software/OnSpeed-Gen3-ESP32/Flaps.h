
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

};
