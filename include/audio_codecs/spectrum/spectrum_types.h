#pragma once
#include <cstdint>
#include <cstddef>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#error "Big-endian architectures require byte swapping for ASV1 file format"
#endif

namespace audio_codecs::spectrum {

constexpr uint32_t ASV_MAGIC = 0x31565341; // "ASV1" in ASCII Little-Endian
constexpr uint16_t ASV_VERSION = 1;
constexpr uint16_t DEFAULT_FFT_SIZE = 1024;
constexpr uint16_t DEFAULT_HOP_SIZE = 512;
constexpr uint8_t  DEFAULT_NUM_BANDS = 64;

enum AsvFlags : uint16_t {
    ASV_FLAG_STEREO   = (1 << 0), // 0 = Mono (summed), 1 = Stereo
    ASV_FLAG_HAS_LODS = (1 << 1), // 1 if LOD hierarchy is present
};

#pragma pack(push, 1)

struct AsvLodDescriptor {
    uint32_t downsample_ratio; // 1 for base LOD 0, 16 for LOD 1
    uint32_t frame_count;      // Total spectrum frames stored in this LOD tier
    uint64_t file_offset;      // Byte offset from start of file to frame data
};

struct AsvHeader {
    uint32_t magic;                    // 0x31565341 ("ASV1")
    uint16_t version;                  // Format version (1)
    uint16_t flags;                    // AsvFlags bitmask
    uint32_t sample_rate;              // Audio sample rate (e.g., 44100, 48000)
    uint8_t  channels;                 // 1 (Mono) or 2 (Stereo)
    uint8_t  num_bands;                // Frequency bands per frame (default: 64)
    uint16_t fft_size;                 // FFT window size (default: 1024)
    uint16_t hop_size;                 // Hop size in samples (default: 512)
    uint16_t min_freq_hz;              // Lower bound of filterbank (default: 20 Hz)
    uint16_t max_freq_hz;              // Upper bound of filterbank (default: 20000 Hz)
    uint16_t lod_count;                // Number of valid LOD tiers (typically 2)
    uint64_t total_pcm_frames;         // Total PCM sample frames in source audio
    uint32_t duration_ms;              // Total track duration in milliseconds
    AsvLodDescriptor lods[4];          // Up to 4 LOD descriptors (64 bytes total)
    uint8_t  reserved[28];             // Reserved padding to align struct to 128 bytes
};

static_assert(sizeof(AsvLodDescriptor) == 16, "AsvLodDescriptor must be exactly 16 bytes");
static_assert(sizeof(AsvHeader) == 128, "AsvHeader must be exactly 128 bytes");

#pragma pack(pop)

} // namespace audio_codecs::spectrum
