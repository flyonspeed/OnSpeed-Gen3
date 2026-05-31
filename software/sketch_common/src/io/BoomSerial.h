
#pragma once

#include <HardwareSerial.h>

#include "src/Globals.h"
#include <util/SnapshotPublisher.h>

#define BOOM_BUFFER_SIZE    127


// BoomSerialIO — UART adapter for the boom AirDAQ probe.  Pumps bytes
// through onspeed::boom::Decode and publishes the decoded floats
// via a lock-free SnapshotPublisher.
//
// Producer task (BoomReadTask, Core 0) reads the UART and calls
// published_.publish() (wait-free).  Consumer task
// (LogSensorCommitTask) reads via Snapshot(out) which wraps
// published_.read() — wait-free in our task layout.
class BoomSerialIO
{
public:
    // Decoded boom data snapshot.  Held in the SnapshotPublisher
    // member below; readers get a value-copy via Snapshot().
    struct SuBoomData {
        float Static  = 0.0f;
        float Dynamic = 0.0f;
        float Alpha   = 0.0f;
        float Beta    = 0.0f;
        float IAS     = 0.0f;
    };

    BoomSerialIO();

    // Public data (still directly accessed for parser-internal buffer
    // and per-link timestamps — these are NOT racy in practice because
    // BoomRead is the only writer and the values are single-word).
    Stream            * pSerial;
    char                Buffer[BOOM_BUFFER_SIZE];
    byte                BufferIndex;
    unsigned long       uTimestamp;          // millis() at last successful decode
    unsigned long       LastReceivedTime;
    int                 MaxAvailable;        // debug

    void Init(Stream * pBoomSerial);
    void Read();

    // Atomic snapshot of the published boom data.  Wait-free read via
    // the SnapshotPublisher's seqcount — typically ~150-200 ns, never
    // blocks the producer.  Safe to call from any task.
    void Snapshot(SuBoomData& out) const;

private:
    onspeed::util::SnapshotPublisher<SuBoomData>  published_;
};