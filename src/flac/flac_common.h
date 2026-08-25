#pragma once
#include "audio_codecs/core/audio_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

// FLAC magic identifier "fLaC" (0x664C6143)
inline constexpr uint8_t FLAC_MAGIC[4] = {'f', 'L', 'a', 'C'};

// Metadata block types (RFC 9639 Section 7.2)
enum class FlacMetadataType : uint8_t {
    StreamInfo    = 0,
    Padding       = 1,
    Application   = 2,
    SeekTable     = 3,
    VorbisComment = 4,
    CueSheet      = 5,
    Picture       = 6,
    Invalid       = 127
};

// Subframe types (RFC 9639 Section 8.3)
enum class FlacSubframeType : uint8_t {
    Constant = 0,
    Verbatim = 1,
    Fixed    = 2,
    Lpc      = 3
};

// Channel assignments (RFC 9639 Section 8.2)
enum class FlacChannelAssignment : uint8_t {
    Independent = 0, // 0..7: 1 to 8 independent channels
    LeftSide    = 8, // Ch 0 = Left, Ch 1 = Side (Left - Right)
    RightSide   = 9, // Ch 0 = Side (Left - Right), Ch 1 = Right
    MidSide     = 10 // Ch 0 = Mid (floor((L+R)/2)), Ch 1 = Side (Left - Right)
};

// STREAMINFO Metadata block structure (RFC 9639 Section 7.3)
struct FlacStreamInfo {
    uint16_t min_block_size{4096};
    uint16_t max_block_size{4096};
    uint32_t min_frame_size{0};
    uint32_t max_frame_size{0};
    uint32_t sample_rate{44100};
    uint8_t  channels{2};
    uint8_t  bits_per_sample{16};
    uint64_t total_samples{0};
    uint8_t  md5_signature[16]{0};
};

// Frame header structure (RFC 9639 Section 8.2)
struct FlacFrameHeader {
    bool     variable_block_size{false};
    uint16_t block_size{4096};
    uint32_t sample_rate{44100};
    FlacChannelAssignment channel_assignment{FlacChannelAssignment::Independent};
    uint8_t  channels{2};
    uint8_t  bits_per_sample{16};
    uint64_t frame_or_sample_number{0};
    uint8_t  crc8{0};
    size_t   header_bytes{0};
};

} // namespace audio_codecs::flac
