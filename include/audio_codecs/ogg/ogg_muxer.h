#pragma once
#include "audio_codecs/ogg/ogg_page.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::ogg {

class OggMuxer {
public:
    explicit OggMuxer(uint32_t serial_number = 0x01020304);
    void reset();

    // Enqueue a packet to be written into Ogg pages
    // Returns true if packet was accepted
    bool write_packet(const uint8_t* packet_data, size_t packet_len, 
                      bool is_bos, bool is_eos, int64_t granule_pos);

    // Flush pending segments into an Ogg page buffer
    // Returns bytes written to out_page, or 0 if no page pending, or negative on error
    int flush_page(uint8_t* out_page, size_t max_out_bytes, bool force = false);

    bool has_pending_page() const;
    uint32_t get_serial_number() const { return serial_number_; }
    void set_serial_number(uint32_t serial_number) { serial_number_ = serial_number; }
    uint32_t get_sequence_number() const { return sequence_number_; }
    void set_sequence_number(uint32_t seq) { sequence_number_ = seq; }

private:
    static constexpr size_t kMaxPagePayload = 65025; // 255 * 255

    uint32_t serial_number_{0};
    uint32_t sequence_number_{0};

    alignas(16) uint8_t current_payload_[kMaxPagePayload];
    size_t payload_len_{0};

    uint8_t segment_table_[OGG_MAX_PAGE_SEGMENTS]{0};
    size_t segment_count_{0};

    int64_t current_granule_pos_{0};
    uint8_t current_header_flags_{0};
};

} // namespace audio_codecs::ogg
