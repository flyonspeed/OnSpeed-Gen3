// BootDiagnostics.h
//
// Captures the reason for every boot (POWERON, BROWNOUT, PANIC, WDT, ...),
// a monotonic boot counter, and how long the previous boot lived, and
// persists them across power cycles in NVS + SD card /boot_log.txt.
//
// On boots that recover from a panic (PANIC, TASK_WDT, INT_WDT, ...) the
// ESP-IDF panic handler has written a binary coredump to the dedicated
// coredump partition before reset. AppendToSd() additionally archives
// that coredump to /coredumps/coredump_<NNNN>_<version>_<task>.bin on the
// SD card and emits an indented panic-summary line under the boot's row
// in /boot_log.txt — enough for a pilot to email us the file plus a
// one-liner that already says what assertion fired and on which task.
//
// Access paths for the pilot:
//   1. Pop the microSD card and read /boot_log.txt on any computer.
//      For panic-class events, also grab the matching .bin from /coredumps/.
//   2. Connect phone to the "OnSpeed" WiFi, browse 192.168.0.1/logs, download.
//   3. USB serial console: type BOOTLOG to tail the file.
//
// Bench-test the panic path with the CRASHME console command — it forces a
// LoadProhibited exception so the next boot exercises the full archival path.
//
#pragma once

class Print;

namespace BootDiag {

// Call once at the very start of setup(), immediately after the FreeRTOS
// semaphores are created. Reads previous-boot state from NVS, increments the
// boot counter, stashes the current boot's reset reason, and prints a
// one-line summary to Serial. Uses g_Log so xSerialLogMutex must exist.
void Init();

// Call after g_SdFileSys.Init() has run. Appends one line describing this
// boot to /boot_log.txt. No-op if SD is unavailable. Safe only to call once
// per boot; subsequent calls are ignored.
void AppendToSd();

// Call periodically from HousekeepingTask (every ~2 s is fine). Internally
// threshold-gated: actually writes to NVS only on the first call after
// crossing each of {5 s, 60 s, 5 min, 30 min, 1 hr} of uptime, for a total
// of five writes per boot. The coarse buckets are plenty for forensic
// "how long did the prior boot live?" while keeping NVS wear — and the
// cache-off stalls that come with flash sector erases — well clear of the
// flight-critical audio and IMU paths.
void Heartbeat();

// Prints the last iMaxLines lines of /boot_log.txt to the given stream.
// Used by the BOOTLOG console command. Takes xWriteMutex for the SD read
// and xSerialLogMutex for the output in that order (non-overlapping —
// W is released before S is taken). DO NOT hold either mutex when calling
// this function — the internal retake of a non-recursive mutex would
// deadlock on ourselves.
void PrintBootLog(Print & out, int iMaxLines);

}  // namespace BootDiag
