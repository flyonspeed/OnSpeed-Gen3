// BoomRead — dedicated UART task for boom probe bytes (PR #609).
//
// Pinned to Core 0, priority 3.  Polls Serial1.available() at 5 ms cadence
// (4× the boom protocol rate, well under the 128 B HW FIFO limit; 1 ms
// polling was tried but overran the PerfLoop ring at 1000 events/sec).
// See BoomRead.cpp for why this stays on Arduino HardwareSerial (rather
// than the raw IDF event-queue path that EfisRead uses).

#pragma once

void BoomReadTask(void *pvParams);
