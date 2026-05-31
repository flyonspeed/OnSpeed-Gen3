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

void WriteFloatLE(std::uint8_t* buf, std::size_t pos, float v) {
    std::memcpy(buf + pos, &v, sizeof(float));   // host is little-endian (x86 / arm64)
}

void WriteDoubleLE(std::uint8_t* buf, std::size_t pos, double v) {
    std::memcpy(buf + pos, &v, sizeof(double));
}

// Epoch-encoded field values.  Each is a different, independently-decodable
// function of the same per-frame counter N.  Used by --epoch-encode mode to
// detect torn reads downstream: every CSV row should yield the same N from
// every field; any disagreement across fields in a single row witnesses a
// tear in the firmware's atomic-publish discipline.
//
// Encoding choices:
//   - Yaw   ∈ [0,    360) ° at 0.01° step — N % 36000 / 100
//   - Pitch ∈ [-30, +30) ° at 0.01° step — different scaling of N (via /7)
//   - Roll  ∈ [-30, +30) ° at 0.01° step — different scaling of N (via *13)
//   - GnssLat = 40.0 + (N % 1_000_000) * 1e-7 — 8-byte double, tests double
//     atomicity specifically (two store instructions on Xtensa LX7)
//   - GnssLon = -105.0 - (N % 1_000_000) * 1e-7 — same idea, different sign
//
// TimeStartupNs already encodes N (it advances by kFrameStepNs each frame),
// so the offline analyzer can use TimeStartupNs/2_500_000 as the canonical N
// and compare every other field's decoded N to it.
float EpochYawDeg(std::uint64_t N) {
    return static_cast<float>((N % 36000ULL)) / 100.0f;  // 0.00 .. 359.99
}
float EpochPitchDeg(std::uint64_t N) {
    return static_cast<float>(((N * 7ULL) % 6000ULL)) / 100.0f - 30.0f;  // -30.00 .. 29.99
}
float EpochRollDeg(std::uint64_t N) {
    return static_cast<float>(((N * 13ULL) % 6000ULL)) / 100.0f - 30.0f;
}
// Lat/Lon encodings.  Step size 1e-6 (not 1e-7) because the OnSpeed CSV
// serializer prints these as %.6f.  At 1e-7, multiple consecutive frames
// rounded to the same printed value — the analyzer couldn't distinguish
// them, producing false-positive tear reports.  With 1e-6 step, every
// N % 100_000 produces a distinct CSV value within the 6-decimal aperture.
// Range: N % 100_000 × 1e-6 = 0..0.099999, modular-wraps every 100K frames
// (250 sec at 400 Hz) — far longer than any single bench run.
double EpochGnssLat(std::uint64_t N) {
    return 40.0 + static_cast<double>(N % 100'000ULL) * 1e-6;
}
double EpochGnssLon(std::uint64_t N) {
    return -105.0 - static_cast<double>(N % 100'000ULL) * 1e-6;
}

void Stamp(std::uint8_t* buf,
           std::uint64_t timeStartupNs, std::uint64_t timeGpsNs,
           bool epochEncode) {
    // Per Vn300.h: TimeStartup at [10..17], TimeGps at [18..25].
    WriteUint64LE(buf, 10, timeStartupNs);
    WriteUint64LE(buf, 18, timeGpsNs);

    if (epochEncode) {
        // Derive N from TimeStartupNs so the analyzer always has one
        // canonical source.  kFrameStepNs = 2_500_000 → N = TimeStartupNs
        // div 2_500_000.  Encode the same N into Yaw/Pitch/Roll (floats)
        // and GnssLat/GnssLon (doubles) so a torn read across any pair
        // becomes detectable offline.
        const std::uint64_t N = timeStartupNs / 2'500'000ULL;
        WriteDoubleLE(buf,  38, EpochGnssLat(N));    // [38..45]
        WriteDoubleLE(buf,  46, EpochGnssLon(N));    // [46..53]
        WriteFloatLE (buf, 100, EpochYawDeg(N));     // [100..103]
        WriteFloatLE (buf, 104, EpochPitchDeg(N));   // [104..107]
        WriteFloatLE (buf, 108, EpochRollDeg(N));    // [108..111]
    }

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
    long frames     = -1;     // -1 = forever
    bool epochEncode = false; // see EpochYawDeg / EpochPitchDeg etc.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            frames = std::atol(argv[++i]);
        } else if (arg == "--epoch-encode") {
            epochEncode = true;
        } else {
            std::fprintf(stderr,
                "usage: %s [--frames N] [--epoch-encode]\n"
                "  emits VN-300 138-byte frames on stdout (binary)\n"
                "\n"
                "  --epoch-encode  Encode the per-frame counter N=TimeStartupNs/2.5ms\n"
                "                  into Yaw/Pitch/Roll/GnssLat/GnssLon. Decodable\n"
                "                  offline so the analyzer can detect torn reads\n"
                "                  in CSV rows (any row where the fields disagree\n"
                "                  on N is a witness of a producer-consumer race).\n",
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
        Stamp(buf, timeStartupNs, timeGpsNs, epochEncode);
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
