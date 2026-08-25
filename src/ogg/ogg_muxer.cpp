#include "audio_codecs/ogg/ogg_muxer.h"
#include "src/ogg/ogg_crc.h"
#include <algorithm>
#include <cstring>

namespace audio_codecs::ogg {

OggMuxer::OggMuxer(uint32_t serial_number) 
    : serial_number_(serial_number) {
    reset();
}

void OggMuxer::reset() {
    sequence_number_ = 0;
    payload_len_ = 0;
    segment_count_ = 0;
    current_granule_pos_ = 0;
    current_header_flags_ = 0;
}

bool OggMuxer::write_packet(const uint8_t* packet_data, size_t packet_len, 
                            bool is_bos, bool is_eos, int64_t granule_pos) {
    if (!packet_data && packet_len > 0) return false;

    // Calculate segments needed
    size_t num_255 = packet_len / 255;
    size_t needed_segs = num_255 + 1; // e.g. 0->1, 254->1, 255->2, 300->2

    if (segment_count_ + needed_segs > OGG_MAX_PAGE_SEGMENTS || 
        payload_len_ + packet_len > kMaxPagePayload) {
        return false; // Caller must flush current page first
    }

    if (is_bos) {
        current_header_flags_ |= OGG_FLAG_BOS;
    }
    if (is_eos) {
        current_header_flags_ |= OGG_FLAG_EOS;
    }

    current_granule_pos_ = granule_pos;

    if (packet_len == 0) {
        segment_table_[segment_count_++] = 0;
        return true;
    }

    size_t offset = 0;
    while (offset < packet_len) {
        size_t seg_len = std::min(static_cast<size_t>(255), packet_len - offset);
        std::memcpy(current_payload_ + payload_len_, packet_data + offset, seg_len);
        payload_len_ += seg_len;
        segment_table_[segment_count_++] = static_cast<uint8_t>(seg_len);
        offset += seg_len;
    }

    // If packet length is exact multiple of 255, append a terminating 0-length segment
    if (packet_len % 255 == 0) {
        segment_table_[segment_count_++] = 0;
    }

    return true;
}

bool OggMuxer::has_pending_page() const {
    return segment_count_ > 0;
}

int OggMuxer::flush_page(uint8_t* out_page, size_t max_out_bytes, bool force) {
    (void)force;
    if (segment_count_ == 0) {
        return 0;
    }

    size_t header_len = OGG_MIN_HEADER_SIZE + segment_count_;
    size_t total_page_len = header_len + payload_len_;

    if (total_page_len > max_out_bytes) {
        return -1; // Buffer too small
    }

    // Build 27-byte header
    out_page[0] = 'O';
    out_page[1] = 'g';
    out_page[2] = 'g';
    out_page[3] = 'S';
    out_page[4] = 0x00; // Stream structure version 0
    out_page[5] = current_header_flags_;

    // Granule position 64-bit little endian
    uint64_t gran = static_cast<uint64_t>(current_granule_pos_);
    for (int b = 0; b < 8; ++b) {
        out_page[6 + b] = static_cast<uint8_t>((gran >> (b * 8)) & 0xFF);
    }

    // Serial number 32-bit little endian
    out_page[14] = static_cast<uint8_t>(serial_number_ & 0xFF);
    out_page[15] = static_cast<uint8_t>((serial_number_ >> 8) & 0xFF);
    out_page[16] = static_cast<uint8_t>((serial_number_ >> 16) & 0xFF);
    out_page[17] = static_cast<uint8_t>((serial_number_ >> 24) & 0xFF);

    // Sequence number 32-bit little endian
    out_page[18] = static_cast<uint8_t>(sequence_number_ & 0xFF);
    out_page[19] = static_cast<uint8_t>((sequence_number_ >> 8) & 0xFF);
    out_page[20] = static_cast<uint8_t>((sequence_number_ >> 16) & 0xFF);
    out_page[21] = static_cast<uint8_t>((sequence_number_ >> 24) & 0xFF);

    // Zero out CRC field initially
    out_page[22] = 0;
    out_page[23] = 0;
    out_page[24] = 0;
    out_page[25] = 0;

    // Segment count
    out_page[26] = static_cast<uint8_t>(segment_count_);

    // Segment table
    std::memcpy(out_page + 27, segment_table_, segment_count_);

    // Payload body
    if (payload_len_ > 0) {
        std::memcpy(out_page + header_len, current_payload_, payload_len_);
    }

    // Compute CRC-32
    uint32_t page_crc = ogg_crc32(0, out_page, total_page_len);
    out_page[22] = static_cast<uint8_t>(page_crc & 0xFF);
    out_page[23] = static_cast<uint8_t>((page_crc >> 8) & 0xFF);
    out_page[24] = static_cast<uint8_t>((page_crc >> 16) & 0xFF);
    out_page[25] = static_cast<uint8_t>((page_crc >> 24) & 0xFF);

    // Increment sequence number and update state for next page
    sequence_number_++;

    // If this page ended with a continued packet (lacing = 255), next page must have CONTINUED flag
    if (segment_count_ > 0 && segment_table_[segment_count_ - 1] == 255) {
        current_header_flags_ = OGG_FLAG_CONTINUED;
    } else {
        current_header_flags_ = 0;
    }

    segment_count_ = 0;
    payload_len_ = 0;
    current_granule_pos_ = -1;

    return static_cast<int>(total_page_len);
}

} // namespace audio_codecs::ogg
