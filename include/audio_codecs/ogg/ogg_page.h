#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::ogg {

constexpr uint8_t OGG_FLAG_CONTINUED = 0x01;
constexpr uint8_t OGG_FLAG_BOS       = 0x02;
constexpr uint8_t OGG_FLAG_EOS       = 0x04;

constexpr size_t OGG_MIN_HEADER_SIZE = 27;
constexpr size_t OGG_MAX_PAGE_SEGMENTS = 255;
constexpr size_t OGG_MAX_PAYLOAD_SIZE = 255 * 255; // 65025 bytes
constexpr size_t OGG_MAX_PAGE_SIZE = OGG_MIN_HEADER_SIZE + OGG_MAX_PAGE_SEGMENTS + OGG_MAX_PAYLOAD_SIZE; // 65307 bytes

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

    bool is_continued() const { return (header_type_flag & OGG_FLAG_CONTINUED) != 0; }
    bool is_bos() const { return (header_type_flag & OGG_FLAG_BOS) != 0; }
    bool is_eos() const { return (header_type_flag & OGG_FLAG_EOS) != 0; }

    size_t header_size() const { return OGG_MIN_HEADER_SIZE + page_segments; }

    size_t payload_size() const {
        size_t total = 0;
        for (size_t i = 0; i < page_segments; ++i) {
            total += segment_table[i];
        }
        return total;
    }

    size_t page_size() const { return header_size() + payload_size(); }
};

} // namespace audio_codecs::ogg
