
#include "src/drivers/SPI_IO.h"
#include "src/Globals.h"
#include <esp_timer.h>
#include <util/Perf.h>

// Per-device SPI clocks on the shared sensor bus.
//
// Honeywell HSC pressure sensors have a datasheet absolute-maximum SPI clock
// of 800 kHz (Honeywell 50066344 Rev. B, Table 1). Datasheet note 1 reads
// "Stresses above these ratings may cause permanent damage." OnSpeed shipped
// with a single 4 MHz global clock until this change, which ran the three
// HSC chips at 5× over their absmax rating.
//
// Per-CS clocks:
//   kCsImu     8 MHz    ISM330DHCX rated to 10 MHz
//   kCsPitot   800 kHz  HSC at datasheet absmax
//   kCsAoa     800 kHz  HSC at datasheet absmax
//   kCsStatic  800 kHz  HSC at datasheet absmax
//   default    4 MHz    catch-all (unused on V4P/V4B today)
//
// The MCP3202 ADC has its own beginTransaction call in Mcp3202Adc.cpp at
// 1 MHz (already in spec; not affected by this file).
#define SPI_MODE          SPI_MODE0

namespace {

inline uint32_t SpiClockForCs(unsigned uChipSel) {
    if (uChipSel == static_cast<unsigned>(kCsImu))    return 8000000;
    if (uChipSel == static_cast<unsigned>(kCsPitot))  return  800000;
    if (uChipSel == static_cast<unsigned>(kCsAoa))    return  800000;
    if (uChipSel == static_cast<unsigned>(kCsStatic)) return  800000;
    return 4000000;
}

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

    pSPI->beginTransaction(SPISettings(SpiClockForCs(uChipSel), MSBFIRST, SPI_MODE));
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
    pSPI->beginTransaction(SPISettings(SpiClockForCs(uChipSel), MSBFIRST, SPI_MODE));
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
    pSPI->beginTransaction(SPISettings(SpiClockForCs(uChipSel), MSBFIRST, SPI_MODE));
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

  pSPI->beginTransaction(SPISettings(SpiClockForCs(uChipSel), MSBFIRST, SPI_MODE));
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
  pSPI->beginTransaction(SPISettings(SpiClockForCs(uChipSel), MSBFIRST, SPI_MODE));
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
    pSPI->beginTransaction(SPISettings(SpiClockForCs(uChipSel), MSBFIRST, SPI_MODE));
    digitalWrite(uChipSel, LOW);
    pSPI->transfer(iAddr);
    pSPI->transferBytes(nullptr, paiData, iBytes);
    digitalWrite(uChipSel, HIGH);
    pSPI->endTransaction();
    }

