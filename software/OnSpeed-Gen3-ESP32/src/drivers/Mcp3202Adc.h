#ifndef MCP3202_ADC_H
#define MCP3202_ADC_H

#include <Arduino.h>

// Reads 12-bit counts (0..4095) from a Microchip MCP3202 (single-ended, MSB first).
// Uses the existing sensor SPI bus (SCLK/MOSI/MISO) and a dedicated CS pin.
//
// Only meaningful on boards where kHasExternalMcp3202 is true (V4P, and any
// future board with the chip wired up). Callers should guard with
// `if constexpr (kHasExternalMcp3202)`. On V4B the compiler eliminates the
// call in dead constexpr branches; the function body is still compiled but
// returns 0 immediately as a belt-and-braces safety net.
uint16_t Mcp3202Read(uint8_t channel);

#endif
