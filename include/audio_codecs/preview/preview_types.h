#pragma once
#include <cstdint>
#include <cstddef>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#error "Big-endian architectures require byte swapping for APV1 file format"
#endif

namespace audio_codecs::preview {

constexpr uint32_t APV_MAGIC = 0x31565041; // "APV1" in ASCII Little-Endian
constexpr uint16_t APV_VERSION = 1;
constexpr uint16_t BASE_CHUNK_FRAMES = 128; // 128 frames = 512 bytes of 16-bit stereo PCM

enum ApvFlags : uint16_t {
    APV_FLAG_STEREO    = (1 << 0), // 0 = Mono, 1 = Stereo
    APV_FLAG_HAS_LODS  = (1 << 1), // 1 if LOD hierarchy is present
};

#pragma pack(push, 1)

struct ApvLodDescriptor {
    uint32_t downsample_ratio{1}; // 1 for LOD 0, 16 for LOD 1, 256 for LOD 2
    uint32_t chunk_count{0};      // Total chunks stored in this LOD tier
    uint64_t file_offset{0};      // Byte offset from start of file to chunk data
};

struct ApvHeader {
    uint32_t magic{APV_MAGIC};         // 0x31565041 ("APV1")
    uint16_t version{APV_VERSION};     // Format version (1)
    uint16_t flags{0};                 // ApvFlags bitmask
    uint32_t sample_rate{44100};       // Audio sample rate (e.g., 44100, 48000)
    uint8_t  channels{2};              // 1 (Mono) or 2 (Stereo)
    uint8_t  bytes_per_chunk{4};       // 2 (Mono) or 4 (Stereo)
    uint16_t samples_per_base_chunk{BASE_CHUNK_FRAMES}; // 128
    uint64_t total_pcm_frames{0};      // Total sample frames in original audio track
    uint32_t duration_ms{0};           // Total track duration in milliseconds
    uint32_t source_file_size{0};      // Original audio file size (for cache validation)
    uint32_t source_header_crc32{0};   // CRC32 of first 4 KB of audio file
    uint8_t  lod_count{0};             // Number of LOD tiers stored (typically 3)
    uint8_t  reserved[11]{0};          // Reserved padding to align LOD descriptors to 8-byte boundary
    ApvLodDescriptor lods[4]{};        // Up to 4 LOD descriptors (64 bytes total)
    uint8_t  padding[16]{0};           // Pads struct to exactly 128 bytes
};

static_assert(sizeof(ApvHeader) == 128, "ApvHeader must be exactly 128 bytes");
static_assert(sizeof(ApvLodDescriptor) == 16, "ApvLodDescriptor must be exactly 16 bytes");

struct WaveformPointMono {
    int8_t min{0}; // Negative trough [-128..0]
    int8_t max{0}; // Positive peak   [0..127]
};

struct WaveformPointStereo {
    int8_t left_min{0};
    int8_t left_max{0};
    int8_t right_min{0};
    int8_t right_max{0};
};

#pragma pack(pop)

} // namespace audio_codecs::preview
