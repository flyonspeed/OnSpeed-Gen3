// test_wav_decode.cpp — Unit tests for WavDecode (PCM/WAV asset handling).
//
// Verifies:
//   - FromRawPcm() correctly wraps headerless byte arrays as PcmAsset views.
//   - DecodeWav() parses RIFF/WAVE byte streams and rejects malformed input.
//   - Each compiled-in OnSpeed PCM voice asset (Audio/PCM_*.h) decodes to
//     a non-empty asset of the expected length and 16 kHz mono format.

#include <unity.h>

#include <audio/WavDecode.h>

#include <cstdint>
#include <cstdio>   // std::snprintf — Linux libstdc++ doesn't pull this transitively
#include <cstring>
#include <vector>

// Pull in every PCM asset header.  Each header defines a `const unsigned
// char NAME_pcm[]` and an `unsigned int NAME_pcm_len`.  Because the
// length symbols are non-const globals we can only include each header
// in one translation unit — this test file is the single home for the
// host-side asset checks.
#include "Audio/PCM_cal_canceled.h"
#include "Audio/PCM_cal_mode.h"
#include "Audio/PCM_cal_saved.h"
#include "Audio/PCM_datamark.h"
#include "Audio/PCM_disabled.h"
#include "Audio/PCM_enabled.h"
#include "Audio/PCM_glimit.h"
#include "Audio/PCM_overg.h"
#include "Audio/PCM_VnoChime.h"
#include "Audio/PCM_left_speaker.h"
#include "Audio/PCM_right_speaker.h"

using onspeed::audio::DecodeWav;
using onspeed::audio::FromRawPcm;
using onspeed::audio::PcmAsset;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// FromRawPcm()
// ============================================================================

void test_from_raw_pcm_null_input(void)
{
    PcmAsset a = FromRawPcm(nullptr, 100, 16000, 1);
    TEST_ASSERT_TRUE(a.empty());
}

void test_from_raw_pcm_zero_len(void)
{
    unsigned char dummy = 0;
    PcmAsset a = FromRawPcm(&dummy, 0, 16000, 1);
    TEST_ASSERT_TRUE(a.empty());
}

void test_from_raw_pcm_one_byte_len(void)
{
    // Less than one int16 sample worth of bytes — empty view.
    unsigned char dummy[1] = { 0 };
    PcmAsset a = FromRawPcm(dummy, 1, 16000, 1);
    TEST_ASSERT_TRUE(a.empty());
}

void test_from_raw_pcm_basic(void)
{
    // 4 bytes = 2 samples
    static const unsigned char data[4] = { 0x10, 0x00, 0x20, 0x00 };
    PcmAsset a = FromRawPcm(data, sizeof(data), 16000, 1);
    TEST_ASSERT_FALSE(a.empty());
    TEST_ASSERT_EQUAL_size_t(2u, a.sampleCount);
    TEST_ASSERT_EQUAL_INT(16000, a.sampleRateHz);
    TEST_ASSERT_EQUAL_INT(1, a.channels);
    TEST_ASSERT_EQUAL_INT16(0x0010, a.samples[0]);
    TEST_ASSERT_EQUAL_INT16(0x0020, a.samples[1]);
}

void test_from_raw_pcm_truncates_odd_byte(void)
{
    // 5 bytes — the trailing odd byte is ignored, count = 2.
    static const unsigned char data[5] = { 0x10, 0x00, 0x20, 0x00, 0xFF };
    PcmAsset a = FromRawPcm(data, sizeof(data), 16000, 1);
    TEST_ASSERT_EQUAL_size_t(2u, a.sampleCount);
}

// ============================================================================
// DecodeWav() — minimal valid WAV
// ============================================================================

// Build a tiny PCM WAV file in memory: 16 kHz mono, two int16 samples.
static std::vector<unsigned char> MakeMinimalWav(std::uint16_t audioFmt = 1,
                                                  std::uint16_t channels = 1,
                                                  std::uint32_t sampleRate = 16000,
                                                  std::uint16_t bitsPerSample = 16)
{
    const std::uint32_t dataLen   = 4;             // 2 int16 samples
    const std::uint32_t fmtSize   = 16;
    const std::uint32_t byteRate  = sampleRate * channels * (bitsPerSample / 8);
    const std::uint16_t blockAlign = channels * (bitsPerSample / 8);
    const std::uint32_t riffSize  = 4 + (8 + fmtSize) + (8 + dataLen);

    std::vector<unsigned char> buf;
    auto put32 = [&](std::uint32_t v){ buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
                                        buf.push_back((v >> 16) & 0xFF); buf.push_back((v >> 24) & 0xFF); };
    auto put16 = [&](std::uint16_t v){ buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); };
    auto putTag = [&](const char* t){ buf.insert(buf.end(), t, t + 4); };

    putTag("RIFF"); put32(riffSize); putTag("WAVE");
    putTag("fmt "); put32(fmtSize);
    put16(audioFmt); put16(channels); put32(sampleRate); put32(byteRate); put16(blockAlign); put16(bitsPerSample);
    putTag("data"); put32(dataLen);
    // Two samples: 100 and -100
    put16(100); put16(static_cast<std::uint16_t>(-100));
    return buf;
}

void test_decode_wav_null_input(void)
{
    PcmAsset a = DecodeWav(nullptr, 100);
    TEST_ASSERT_TRUE(a.empty());
}

void test_decode_wav_too_short(void)
{
    unsigned char buf[10] = { 0 };
    PcmAsset a = DecodeWav(buf, sizeof(buf));
    TEST_ASSERT_TRUE(a.empty());
}

void test_decode_wav_bad_magic(void)
{
    auto wav = MakeMinimalWav();
    wav[0] = 'X';   // corrupt RIFF magic
    PcmAsset a = DecodeWav(wav.data(), wav.size());
    TEST_ASSERT_TRUE(a.empty());
}

void test_decode_wav_minimal_valid(void)
{
    auto wav = MakeMinimalWav();
    PcmAsset a = DecodeWav(wav.data(), wav.size());
    TEST_ASSERT_FALSE(a.empty());
    TEST_ASSERT_EQUAL_size_t(2u, a.sampleCount);
    TEST_ASSERT_EQUAL_INT(16000, a.sampleRateHz);
    TEST_ASSERT_EQUAL_INT(1, a.channels);
    TEST_ASSERT_EQUAL_INT16(100, a.samples[0]);
    TEST_ASSERT_EQUAL_INT16(-100, a.samples[1]);
}

void test_decode_wav_rejects_non_pcm(void)
{
    // audioFmt 3 = IEEE float; we require PCM (1).
    auto wav = MakeMinimalWav(3);
    PcmAsset a = DecodeWav(wav.data(), wav.size());
    TEST_ASSERT_TRUE(a.empty());
}

void test_decode_wav_rejects_24bit(void)
{
    auto wav = MakeMinimalWav(1, 1, 16000, 24);
    PcmAsset a = DecodeWav(wav.data(), wav.size());
    TEST_ASSERT_TRUE(a.empty());
}

void test_decode_wav_stereo_ok(void)
{
    auto wav = MakeMinimalWav(1, 2, 44100, 16);
    PcmAsset a = DecodeWav(wav.data(), wav.size());
    TEST_ASSERT_FALSE(a.empty());
    TEST_ASSERT_EQUAL_INT(2, a.channels);
    TEST_ASSERT_EQUAL_INT(44100, a.sampleRateHz);
}

// ============================================================================
// Compiled-in PCM asset checks: each xxd-converted .pcm header should
// produce a non-empty PcmAsset of the expected length at 16 kHz mono.
// ============================================================================

namespace {

struct AssetEntry {
    const char*         name;
    const unsigned char* bytes;
    unsigned int        len;
};

const AssetEntry kAssets[] = {
    {"datamark",      datamark_pcm,      datamark_pcm_len     },
    {"disabled",      disabled_pcm,      disabled_pcm_len     },
    {"enabled",       enabled_pcm,       enabled_pcm_len      },
    {"glimit",        glimit_pcm,        glimit_pcm_len       },
    {"cal_canceled",  cal_canceled_pcm,  cal_canceled_pcm_len },
    {"cal_mode",      cal_mode_pcm,      cal_mode_pcm_len     },
    {"cal_saved",     cal_saved_pcm,     cal_saved_pcm_len    },
    {"overg",         overg_pcm,         overg_pcm_len        },
    {"VnoChime",      VnoChime_pcm,      VnoChime_pcm_len     },
    {"left_speaker",  left_speaker_pcm,  left_speaker_pcm_len },
    {"right_speaker", right_speaker_pcm, right_speaker_pcm_len},
};
constexpr std::size_t kAssetCount = sizeof(kAssets) / sizeof(kAssets[0]);

}   // namespace

void test_assets_all_decode_nonempty(void)
{
    for (std::size_t i = 0; i < kAssetCount; ++i) {
        const AssetEntry& e = kAssets[i];
        PcmAsset a = FromRawPcm(e.bytes, e.len, 16000, 1);
        char msg[64];
        std::snprintf(msg, sizeof(msg), "asset '%s' decoded empty", e.name);
        TEST_ASSERT_FALSE_MESSAGE(a.empty(), msg);
        TEST_ASSERT_EQUAL_size_t(e.len / 2u, a.sampleCount);
        TEST_ASSERT_EQUAL_INT(16000, a.sampleRateHz);
        TEST_ASSERT_EQUAL_INT(1, a.channels);
    }
}

void test_assets_lengths_are_even(void)
{
    // Every PCM asset is int16 — byte length must be a multiple of 2 or
    // we'd be silently dropping the trailing byte (a sign of corruption).
    for (std::size_t i = 0; i < kAssetCount; ++i) {
        const AssetEntry& e = kAssets[i];
        char msg[64];
        std::snprintf(msg, sizeof(msg), "asset '%s' has odd byte length", e.name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, e.len % 2u, msg);
    }
}

void test_assets_lengths_match_known_values(void)
{
    // Snapshot of the as-shipped PCM lengths.  Updating any audio asset
    // requires updating this table — that's intentional, so a re-recorded
    // voice clip with a different length forces a code-review touchpoint.
    struct Expected { const char* name; unsigned int len; };
    const Expected kExpected[] = {
        {"datamark",      15560},
        {"disabled",      32850},
        {"enabled",       27764},
        {"glimit",        15708},
        {"cal_canceled",  69120},
        {"cal_mode",      53760},
        {"cal_saved",     65280},
        {"overg",         72960},
        {"VnoChime",      39288},
        {"left_speaker",  41794},
        {"right_speaker", 38360},
    };
    constexpr std::size_t kExpectedCount = sizeof(kExpected) / sizeof(kExpected[0]);

    TEST_ASSERT_EQUAL_size_t(kExpectedCount, kAssetCount);

    for (std::size_t i = 0; i < kAssetCount; ++i) {
        // Find expected entry by name (assets are listed in the same
        // order in both arrays, but a name-keyed search is robust to
        // future reordering).
        bool found = false;
        for (std::size_t j = 0; j < kExpectedCount; ++j) {
            if (std::strcmp(kAssets[i].name, kExpected[j].name) == 0) {
                char msg[80];
                std::snprintf(msg, sizeof(msg),
                              "asset '%s' length changed: expected %u",
                              kAssets[i].name, kExpected[j].len);
                TEST_ASSERT_EQUAL_UINT_MESSAGE(kExpected[j].len, kAssets[i].len, msg);
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, "asset name not in expected table");
    }
}

void test_assets_have_audio_content(void)
{
    // Sanity: at least one nonzero sample (caller-paranoia against an
    // accidental all-zeros asset).
    for (std::size_t i = 0; i < kAssetCount; ++i) {
        const AssetEntry& e = kAssets[i];
        PcmAsset a = FromRawPcm(e.bytes, e.len, 16000, 1);
        bool nonzero = false;
        for (std::size_t k = 0; k < a.sampleCount; ++k) {
            if (a.samples[k] != 0) { nonzero = true; break; }
        }
        char msg[64];
        std::snprintf(msg, sizeof(msg), "asset '%s' is all zeros", e.name);
        TEST_ASSERT_TRUE_MESSAGE(nonzero, msg);
    }
}

// ============================================================================

int main(int, char**)
{
    UNITY_BEGIN();

    RUN_TEST(test_from_raw_pcm_null_input);
    RUN_TEST(test_from_raw_pcm_zero_len);
    RUN_TEST(test_from_raw_pcm_one_byte_len);
    RUN_TEST(test_from_raw_pcm_basic);
    RUN_TEST(test_from_raw_pcm_truncates_odd_byte);

    RUN_TEST(test_decode_wav_null_input);
    RUN_TEST(test_decode_wav_too_short);
    RUN_TEST(test_decode_wav_bad_magic);
    RUN_TEST(test_decode_wav_minimal_valid);
    RUN_TEST(test_decode_wav_rejects_non_pcm);
    RUN_TEST(test_decode_wav_rejects_24bit);
    RUN_TEST(test_decode_wav_stereo_ok);

    RUN_TEST(test_assets_all_decode_nonempty);
    RUN_TEST(test_assets_lengths_are_even);
    RUN_TEST(test_assets_lengths_match_known_values);
    RUN_TEST(test_assets_have_audio_content);

    return UNITY_END();
}
