// EfisRead — dedicated UART task for EFIS bytes (PR #609).
//
// Pinned to Core 0, priority 3.  Blocks on the IDF UART event queue and
// calls g_EfisSerial.Read() per UART_DATA event, so the parser wakes
// immediately when bytes arrive instead of being driven by loop()'s
// busy-poll. See EfisRead.cpp for the rationale and implementation.

#pragma once

#include <cstdint>

void EfisReadTask(void *pvParams);

// Install the IDF UART driver for the EFIS port at the given baud rate.
// VN-300 needs 921600 (for 138 B × 400 Hz = 55.2 kB/s); all other EFIS
// types use 115200. Caller chooses based on g_EfisSerial.enType.
bool EfisReadTaskInit(uint32_t baud);
