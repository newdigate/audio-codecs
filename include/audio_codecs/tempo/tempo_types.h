#pragma once

#include <cstdint>
#include <cstddef>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#error "Big-endian architectures require byte swapping for ATT1 file format"
#endif

namespace audio_codecs::tempo {

constexpr uint8_t ATT_MAGIC[4] = {'A', 'T', 'T', '1'};
constexpr uint16_t ATT_VERSION = 1;
constexpr uint16_t ATT_HEADER_SIZE = 128;

enum AttHeaderFlags : uint8_t {
    ATT_FLAG_CONSTANT_TEMPO = 0x01,
    ATT_FLAG_HAS_DOWNBEATS  = 0x02,
    ATT_FLAG_USER_MODIFIED  = 0x04
};

enum AttBeatFlags : uint16_t {
    ATT_BEAT_FLAG_DOWNBEAT     = 0x0001,
    ATT_BEAT_FLAG_INTERPOLATED = 0x0002,
    ATT_BEAT_FLAG_USER_EDITED  = 0x0004,
    ATT_BEAT_FLAG_LOW_CONF     = 0x0008
};

#pragma pack(push, 1)

struct AttTempoPoint {
    uint32_t time_ms;
    uint32_t bpm_q16;
};
static_assert(sizeof(AttTempoPoint) == 8, "AttTempoPoint must be 8 bytes");

struct AttBeatMarker {
    uint32_t time_ms;
    uint32_t bar_index;
    uint16_t beat_within_bar;
    uint16_t flags;
    uint32_t local_bpm_q16;
};
static_assert(sizeof(AttBeatMarker) == 16, "AttBeatMarker must be 16 bytes");

struct AttHeader {
    uint8_t  magic[4];
    uint16_t version;
    uint16_t header_size;
    uint32_t duration_ms;
    uint32_t sample_rate;
    uint32_t global_bpm_q16;
    uint8_t  confidence;
    uint8_t  time_signature_num;
    uint8_t  time_signature_denom;
    uint8_t  flags;
    uint32_t first_beat_ms;
    uint32_t first_downbeat_ms;
    uint32_t total_beats;
    uint32_t total_bars;

    uint64_t tempo_curve_offset;
    uint32_t tempo_curve_count;
    uint16_t tempo_point_size;
    uint16_t reserved0;

    uint64_t beat_grid_offset;
    uint32_t beat_grid_count;
    uint16_t beat_marker_size;
    uint16_t reserved1;

    uint8_t  reserved[56];
};
static_assert(sizeof(AttHeader) == 128, "AttHeader must be exactly 128 bytes");

#pragma pack(pop)

} // namespace audio_codecs::tempo
