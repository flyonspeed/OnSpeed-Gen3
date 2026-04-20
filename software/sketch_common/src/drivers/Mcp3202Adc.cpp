#include "src/drivers/Mcp3202Adc.h"
#include "src/Globals.h"

static constexpr uint32_t kMcp3202SpiClockHz = 1000000;

uint16_t Mcp3202Read(uint8_t channel)
{
    if constexpr (kHasExternalMcp3202)
        {
        if (g_pSensorSPI == nullptr || g_pSensorSPI->pSPI == nullptr)
            return 0;

        channel &= 0x01; // MCP3202 has channels 0..1

        // 3-byte MCP3202 transaction (SPI Mode 0, MSB first).
        // Byte 0: start bit
        // Byte 1: SGL=1, ODD/SIGN=channel, MSBF=1, then zeros
        // Byte 2: clock out remaining data bits
        const uint8_t configByte = uint8_t(0xA0 | (channel << 6)); // CH0=0xA0, CH1=0xE0

        g_pSensorSPI->pSPI->beginTransaction(SPISettings(kMcp3202SpiClockHz, MSBFIRST, SPI_MODE0));
        digitalWrite(kCsAdc, LOW);

        (void)g_pSensorSPI->pSPI->transfer(0x01);                // start bit
        uint8_t rx1 = g_pSensorSPI->pSPI->transfer(configByte);  // config; returns null + B11..B8
        uint8_t rx2 = g_pSensorSPI->pSPI->transfer(0x00);        // returns B7..B0

        digitalWrite(kCsAdc, HIGH);
        g_pSensorSPI->pSPI->endTransaction();

        return uint16_t(((rx1 & 0x0F) << 8) | rx2);
        }
    return 0;   // No external ADC on this board.
}
