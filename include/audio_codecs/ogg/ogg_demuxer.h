#pragma once
#include "audio_codecs/ogg/ogg_page.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::ogg {

class OggDemuxer {
public:
    OggDemuxer();
    void reset();

    // Push new bytes from container stream into demuxer
    // Returns true on success, updates bytes_consumed with number of bytes parsed/stored
    bool push_bytes(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed);

    // Attempt to extract the next complete logical Vorbis packet
    // Returns packet byte size, or 0 if more data is needed, or negative on error
    int read_packet(uint8_t* out_packet, size_t max_out_bytes, 
                    int64_t& out_granule_pos, bool& out_is_bos, bool& out_is_eos);

    bool has_packet() const;
    uint32_t get_serial_number() const { return current_header_.bitstream_serial_number; }
    uint32_t get_sequence_number() const { return current_header_.page_sequence_number; }
    int64_t get_last_granule_pos() const { return current_header_.granule_position; }
    bool is_bos() const { return (current_header_.header_type_flag & OGG_FLAG_BOS) != 0; }
    bool is_eos() const { return (current_header_.header_type_flag & OGG_FLAG_EOS) != 0; }

private:
    static constexpr size_t kBufferSize = 131072;
    static constexpr size_t kMaxPacketSize = 65536;

    alignas(16) uint8_t buffer_[kBufferSize];
    size_t buf_len_{0};

    OggPageHeader current_header_{};
    size_t current_seg_index_{0};
    size_t page_body_offset_{0};
    size_t page_total_size_{0};
    bool in_page_{false};

    // Packet assembly state
    alignas(16) uint8_t packet_assembly_[kMaxPacketSize];
    size_t packet_assembly_len_{0};
    int64_t packet_granule_pos_{0};
    bool packet_is_bos_{false};
    bool packet_is_eos_{false};
    bool packet_ready_{false};

    bool parse_next_page();
    bool assemble_packet();
};

} // namespace audio_codecs::ogg
