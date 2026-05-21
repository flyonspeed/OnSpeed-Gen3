// EfisRead — dedicated UART task for EFIS bytes (PR #609).
//
// Pinned to Core 0, priority 3.  Blocks on the IDF UART event queue and
// calls g_EfisSerial.Read() per UART_DATA event, so the parser wakes
// immediately when bytes arrive instead of being driven by loop()'s
// busy-poll. See EfisRead.cpp for the rationale and implementation.

#pragma once

void EfisReadTask(void *pvParams);
