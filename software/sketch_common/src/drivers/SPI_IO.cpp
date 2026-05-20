
#include "src/drivers/SPI_IO.h"
#include "src/Globals.h"
#include <esp_timer.h>
#include <util/Perf.h>

#define SPI_CLK              4000000  // 4 MHz (ISM330DHCX rated 10 MHz, HSC sensors ~8 MHz)
#define SPI_MODE          SPI_MODE0

namespace {

// Map chip-select pin → ScopeId. Returns SpiSd (a catch-all) for any
// pin we don't recognise so unknown traffic still gets counted.
inline onspeed::util::perf::ScopeId scopeForCs(unsigned uChipSel) {
    using onspeed::util::perf::ScopeId;
    if (uChipSel == static_cast<unsigned>(kCsImu))    return ScopeId::SpiImu;
    if (uChipSel == static_cast<unsigned>(kCsAoa))    return ScopeId::SpiAoa;
    if (uChipSel == static_cast<unsigned>(kCsPitot))  return ScopeId::SpiPitot;
    if (uChipSel == static_cast<unsigned>(kCsStatic)) return ScopeId::SpiStatic;
    return ScopeId::SpiSd;  // catch-all incl. SD card and any future CS.
}

class SpiPerfTimer {
public:
    SpiPerfTimer(unsigned uChipSel, uint32_t bytes)
        : scopeId_(scopeForCs(uChipSel)),
          bytes_(bytes),
          startUs_(onspeed::util::perf::perfEnabled() ? esp_timer_get_time() : 0) {}
    ~SpiPerfTimer() {
        if (startUs_ != 0) {
            const uint64_t end = esp_timer_get_time();
            onspeed::util::perf::recordSpiTransfer(
                scopeId_, bytes_, static_cast<uint32_t>(end - startUs_));
        }
    }
private:
    onspeed::util::perf::ScopeId scopeId_;
    uint32_t bytes_;
    int64_t  startUs_;
};

}  // namespace

SpiIO::SpiIO(int SPINum, int ClkPin, int MisoPin, int MosiPin, unsigned DummyCS)
{
  pSPI = new SPIClass(SPINum);
  pSPI->begin(ClkPin, MisoPin, MosiPin, DummyCS);  //SCLK, MISO, MOSI, SS
}

// ----------------------------------------------------------------------------

uint8_t SpiIO::ReadByte( unsigned uChipSel)
{
    uint8_t   iData;
    SpiPerfTimer perfT(uChipSel, /*bytes=*/1);

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
    SpiPerfTimer perfT(uChipSel, /*bytes=*/1);
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
    SpiPerfTimer perfT(uChipSel, static_cast<uint32_t>(iBytes));
    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    pSPI->transferBytes(nullptr, paiData, iBytes);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
}
// ----------------------------------------------------------------------------

uint8_t SpiIO::ReadRegByte( unsigned uChipSel, uint8_t iAddr)
{
  uint8_t   iData;
  SpiPerfTimer perfT(uChipSel, /*bytes=*/2);

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
  SpiPerfTimer perfT(uChipSel, /*bytes=*/2);
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
    SpiPerfTimer perfT(uChipSel, static_cast<uint32_t>(iBytes + 1));
    pSPI->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    pSPI->transfer(iAddr);
    pSPI->transferBytes(nullptr, paiData, iBytes);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
    }

