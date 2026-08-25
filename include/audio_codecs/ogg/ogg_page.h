#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::ogg {

constexpr uint8_t OGG_FLAG_CONTINUED = 0x01;
constexpr uint8_t OGG_FLAG_BOS       = 0x02;
constexpr uint8_t OGG_FLAG_EOS       = 0x04;

constexpr size_t OGG_MIN_HEADER_SIZE = 27;
constexpr size_t OGG_MAX_PAGE_SEGMENTS = 255;
constexpr size_t OGG_MAX_PAGE_SIZE = 65307; // 27 + 255 + 255 * 255

struct OggPageHeader {
    uint8_t capture_pattern[4]{'O', 'g', 'g', 'S'};
    uint8_t stream_structure_version{0};
    uint8_t header_type_flag{0};
    int64_t granule_position{0};
    uint32_t bitstream_serial_number{0};
    uint32_t page_sequence_number{0};
    uint32_t crc_checksum{0};
    uint8_t page_segments{0};
    uint8_t segment_table[OGG_MAX_PAGE_SEGMENTS]{0};
};

} // namespace audio_codecs::ogg
