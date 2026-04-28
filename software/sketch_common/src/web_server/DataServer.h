
#ifndef LIVE_DATA_SERVER_H
#define LIVE_DATA_SERVER_H

#include <cstddef>
#include <cstdint>

void DataServerInit();
void DataServerPoll();

// Broadcast one #1 display-serial frame as a binary WebSocket message
// to all connected clients on port 81. Called from DisplaySerial::Write()
// immediately after the frame is sent over UART, so the WebSocket bytes
// are byte-identical to what the M5 hardware receives.
//
// Cheap when no clients are connected (early-out inside the broadcaster).
// On clients connected, the cost is one ~74-byte alloc + send per call.
void BroadcastDisplayFrame(const uint8_t* frame, size_t len);


#endif
