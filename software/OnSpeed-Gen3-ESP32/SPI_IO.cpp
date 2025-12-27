
#include "SPI_IO.h"

#define SPI_CLK              1000000  // 100 kHz
#define SPI_MODE          SPI_MODE0

SpiIO::SpiIO(int SPINum, int ClkPin, int MisoPin, int MosiPin, unsigned DummyCS)
{
  pSPI = new SPIClass(SPINum);
  pSPI->begin(ClkPin, MisoPin, MosiPin, DummyCS);  //SCLK, MISO, MOSI, SS
}

// ----------------------------------------------------------------------------

uint8_t SpiIO::ReadByte( unsigned uChipSel)
{
    uint8_t   iData;

    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    iData = pSPI->transfer(0x00);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();

    return iData;
}

// ----------------------------------------------------------------------------

void SpiIO::WriteByte(unsigned uChipSel, uint8_t iData)
{
    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    pSPI->transfer(iData);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
}

#if 0
// ----------------------------------------------------------------------------

uint16_t SpiIO::ReadWord( unsigned uChipSel)
{

}

// ----------------------------------------------------------------------------

void SpiIO::WriteWord(unsigned uChipSel, uint16_t iData)
{

}
#endif

void SpiIO::ReadBytes( unsigned uChipSel, uint8_t * paiData, int iBytes)
{
    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    for (int iIdx = 0; iIdx < iBytes; iIdx++)
      paiData[iIdx] = pSPI->transfer(0x00);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
}
// ----------------------------------------------------------------------------

uint8_t SpiIO::ReadRegByte( unsigned uChipSel, uint8_t iAddr)
{
  uint8_t   iData;

  pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
  digitalWrite(uChipSel, LOW);
//        pSPI->transfer((byte)(0x80 | bAddr));
          pSPI->transfer(iAddr);
  iData = pSPI->transfer(0x00);
  digitalWrite(uChipSel, HIGH);  //pull ss high to signify end of data transfer
  pSPI->endTransaction();

  return iData;
}

// ----------------------------------------------------------------------------

void SpiIO::WriteRegByte(unsigned uChipSel, uint8_t iAddr, byte iData)
{
  pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
  digitalWrite(uChipSel, LOW);  //pull SS slow to prep other end for transfer
//  pSPI->transfer((byte)(0x7F & bAddr));
  pSPI->transfer(iAddr);
  pSPI->transfer(iData);
  digitalWrite(uChipSel, HIGH);  //pull ss high to signify end of data transfer
  pSPI->endTransaction();
}

// ----------------------------------------------------------------------------

void SpiIO::ReadRegBytes(unsigned uChipSel, uint8_t iAddr, uint8_t * paiData, int iBytes)
    {
    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    pSPI->transfer(iAddr);
    for (int iIdx = 0; iIdx < iBytes; iIdx++)
        paiData[iIdx] = pSPI->transfer(0x00);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
    }

// ----------------------------------------------------------------------------

void SpiIO::WriteRegBytes(unsigned uChipSel, uint8_t iAddr, uint8_t * paiData, int iBytes)
    {
    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    pSPI->transfer(iAddr);
    for (int iIdx = 0; iIdx < iBytes; iIdx++)
        pSPI->transfer(paiData[iIdx]);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
    }

