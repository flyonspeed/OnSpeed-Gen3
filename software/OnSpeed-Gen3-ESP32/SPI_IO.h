
#include <Arduino.h>
#include <SPI.h>

#ifndef SPI_IO_H
#define SPI_IO_H

class SpiIO
{
public:
    SpiIO(int SPINum, int ClkPin, int MisoPin, int MosiPin, unsigned DummyCS);

public:
    SPIClass * pSPI;

public:
    uint8_t   ReadByte( unsigned uChipSel);
    void      WriteByte(unsigned uChipSel, uint8_t iData);

//    uint16_t  ReadWord( unsigned uChipSel);
//    void      WriteWord(unsigned uChipSel, uint16_t iData);

    void      ReadBytes( unsigned uChipSel, uint8_t * paiData, int iBytes);
    uint8_t   ReadRegByte( unsigned uChipSel, uint8_t iAddr);
    void      WriteRegByte(unsigned uChipSel, uint8_t iAddr, uint8_t iData);

    void      ReadRegBytes( unsigned uChipSel, uint8_t bAddr, uint8_t * paiData, int iBytes);
    void      WriteRegBytes(unsigned uChipSel, uint8_t bAddr, uint8_t * paiData, int iBytes);

};

#endif