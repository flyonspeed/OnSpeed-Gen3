// efis_stim_vn300 — emit VN-300 binary frames on stdout for the UART stim rig.
//
// Why a C++ helper instead of packing bytes in Python: the on-wire frame layout
// (header, group masks, field offsets, VN CRC-16) is owned by
// onspeed_core/test_frames/SynthFrames.cpp, which is the same builder
// exercised by test_efis_vn300. Re-implementing it in Python invites drift on
// every wire-format bump. We instead link the same builder and just bump the
// two per-frame timestamps before emission so the parser sees advancing time.
//
// Output: raw little-endian binary on stdout. Each frame is exactly 138 bytes.
// No headers, no newlines, no framing of our own — the VN-300 sync sequence
// (0xFA 0x1B) is already at the start of each frame and is the resync point
// the firmware parser uses.
//
// Usage:
//     efis_stim_vn300 [--frames N]
//
// With no args: emits forever (the Python driver paces it via blocking writes
// to the serial port). With --frames N: emits N frames then exits cleanly,
// useful for golden-byte capture and unit-style checks.
//
// Build: see CMakeLists.txt in this directory. No PlatformIO involvement —
// this is a host tool. The CMake target depends only on a few onspeed_core
// sources; we don't want to drag in the Arduino-flavored sketch tree.

#include <test_frames/SynthFrames.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include <unistd.h>   // write(2), STDOUT_FILENO

namespace {

constexpr std::size_t kVn300Len = 138;

// VN CRC-16, byte-wise reference algorithm. Same one ComputeVnCrc in
// SynthFrames uses; duplicated here because that function is in an anonymous
// namespace and we don't want to plumb a public API just for the stim. If
// SynthFrames ever exposes ComputeVnCrc publicly, drop this duplicate.
// Match validated by emitting a baseline frame and CRC-checking against
// the static buffer from onspeed::test_frames::Vn300Frame().
std::uint16_t ComputeVnCrc(const std::uint8_t* buf,
                           std::size_t begin, std::size_t end) {
    std::uint16_t crc = 0;
    for (std::size_t i = begin; i < end; ++i) {
        crc = static_cast<std::uint16_t>((crc >> 8) | (crc << 8));
        crc ^= buf[i];
        crc ^= static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(crc & 0xFF) >> 4);
        crc ^= static_cast<std::uint16_t>((crc << 8) << 4);
        crc ^= static_cast<std::uint16_t>(((crc & 0xFF) << 4) << 1);
    }
    return crc;
}

void WriteUint64LE(std::uint8_t* buf, std::size_t pos, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        buf[pos + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}

void Stamp(std::uint8_t* buf,
           std::uint64_t timeStartupNs, std::uint64_t timeGpsNs) {
    // Per Vn300.h: TimeStartup at [10..17], TimeGps at [18..25].
    WriteUint64LE(buf, 10, timeStartupNs);
    WriteUint64LE(buf, 18, timeGpsNs);

    // CRC covers bytes 1..135 inclusive (i.e. [1, 136)); result lives in
    // [136, 137] big-endian. Header byte 0 (sync 0xFA) is NOT part of the
    // CRC — matches Vn300.cpp::vnCrc16 calling with start=1.
    const std::uint16_t crc = ComputeVnCrc(buf, 1, 136);
    buf[136] = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    buf[137] = static_cast<std::uint8_t>(crc & 0xFF);
}

// Write exactly `n` bytes or die. write(2) is allowed to short-write on
// pipes; we loop until done or fail hard.
bool WriteAll(int fd, const void* data, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    long frames = -1;  // -1 = forever
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            frames = std::atol(argv[++i]);
        } else {
            std::fprintf(stderr,
                "usage: %s [--frames N]\n"
                "  emits VN-300 138-byte frames on stdout (binary)\n",
                argv[0]);
            return 2;
        }
    }

    // Seed buffer from the canonical SynthFrames builder so payload bytes
    // are bit-identical to what unit tests validate.
    const auto& src = onspeed::test_frames::Vn300Frame();
    if (src.len != kVn300Len) {
        std::fprintf(stderr,
            "efis_stim_vn300: unexpected frame size %zu (want %zu)\n",
            src.len, kVn300Len);
        return 1;
    }
    std::uint8_t buf[kVn300Len];
    std::memcpy(buf, src.bytes, kVn300Len);

    // Time bases: walk forward at 400 Hz (2.5 ms per frame). The absolute
    // start values don't matter to the firmware (firmware just decodes and
    // logs); the rate of advance does, because Vac will see these
    // deltas in vnTimeStartupNs columns in the CSV and the spacing is the
    // signal that confirms the firehose actually arrived.
    constexpr std::uint64_t kFrameStepNs = 2'500'000ULL;  // 2.5 ms

    // Pull initial timestamps out of the seed frame so emitted values
    // form a continuous sequence from the SynthFrames baseline.
    std::uint64_t timeStartupNs = 0;
    std::uint64_t timeGpsNs     = 0;
    for (int i = 0; i < 8; ++i) {
        timeStartupNs |= static_cast<std::uint64_t>(buf[10 + i]) << (8 * i);
        timeGpsNs     |= static_cast<std::uint64_t>(buf[18 + i]) << (8 * i);
    }

    long emitted = 0;
    while (frames < 0 || emitted < frames) {
        Stamp(buf, timeStartupNs, timeGpsNs);
        if (!WriteAll(STDOUT_FILENO, buf, kVn300Len)) {
            // Broken pipe is the normal exit path when the Python driver
            // closes stdin on Ctrl-C; treat it as success.
            return 0;
        }
        timeStartupNs += kFrameStepNs;
        timeGpsNs     += kFrameStepNs;
        ++emitted;
    }
    return 0;
}
