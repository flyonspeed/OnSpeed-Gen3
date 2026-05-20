// PerfDump.h — 1 Hz task that drains the Perf rings and emits a
// human-readable summary to USB serial.
//
// Only built when ONSPEED_PERF_ENABLED is defined. The console
// command `perf on|off|dump` calls the symbols below.

#pragma once

#include <util/Perf.h>

#ifdef ONSPEED_PERF_ENABLED

namespace onspeed::perf_dump {

// Start the PerfDumpTask. Idempotent; subsequent calls are no-ops.
// Internally pinned to Core 0 at priority 0 (low) so it never preempts
// the audio/IMU hot paths.
void StartTask();

// Emit a one-shot snapshot to the USB serial console regardless of
// streaming state. Used by the `perf dump` console command.
void EmitOneShot();

// Toggle streaming mode. When on, the dump task emits a snapshot to
// USB serial every second. When off, the task still drains the rings
// (so they don't overflow) but doesn't print anything.
void SetStreaming(bool on);
bool IsStreaming();

}  // namespace onspeed::perf_dump

#endif  // ONSPEED_PERF_ENABLED
