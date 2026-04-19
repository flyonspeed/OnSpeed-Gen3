#ifndef MCP3202_ADC_H
#define MCP3202_ADC_H

#include <Arduino.h>

// Read a 12-bit sample (0..4095) from the external MCP3202 ADC (single-ended,
// MSB first). Uses the shared sensor SPI bus and the kCsAdc chip-select pin.
// Returns 0 on boards without the external ADC (kHasExternalMcp3202 == false).
uint16_t Mcp3202Read(uint8_t channel);

#endif
