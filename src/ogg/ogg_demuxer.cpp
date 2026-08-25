#include "audio_codecs/ogg/ogg_demuxer.h"
#include "src/ogg/ogg_crc.h"
#include <algorithm>
#include <cstring>

namespace audio_codecs::ogg {

OggDemuxer::OggDemuxer() {
    reset();
}

void OggDemuxer::reset() {
    buf_len_ = 0;
    std::memset(&current_header_, 0, sizeof(current_header_));
    current_seg_index_ = 0;
    page_body_offset_ = 0;
    page_total_size_ = 0;
    in_page_ = false;
    packet_assembly_len_ = 0;
    packet_granule_pos_ = 0;
    packet_is_bos_ = false;
    packet_is_eos_ = false;
    packet_ready_ = false;
}

bool OggDemuxer::push_bytes(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed) {
    if (!in_data || in_bytes == 0) {
        bytes_consumed = 0;
        return true;
    }

    size_t space_left = sizeof(buffer_) - buf_len_;
    size_t to_copy = std::min(in_bytes, space_left);
    if (to_copy > 0) {
        std::memcpy(buffer_ + buf_len_, in_data, to_copy);
        buf_len_ += to_copy;
    }
    bytes_consumed = to_copy;

    // Process available pages and assemble packets
    while (!packet_ready_) {
        if (!in_page_) {
            if (!parse_next_page()) {
                break;
            }
        }
        if (in_page_) {
            if (!assemble_packet()) {
                break;
            }
        }
    }

    return true;
}

bool OggDemuxer::parse_next_page() {
    if (buf_len_ < OGG_MIN_HEADER_SIZE) {
        return false;
    }

    // Search for sync capture "OggS"
    size_t sync_pos = 0;
    bool found_sync = false;
    for (size_t i = 0; i + 3 < buf_len_; ++i) {
        if (buffer_[i] == 'O' && buffer_[i + 1] == 'g' && 
            buffer_[i + 2] == 'g' && buffer_[i + 3] == 'S') {
            sync_pos = i;
            found_sync = true;
            break;
        }
    }

    if (!found_sync) {
        // Discard buffer except last 3 bytes (in case partial "Ogg" is at end)
        if (buf_len_ > 3) {
            std::memmove(buffer_, buffer_ + (buf_len_ - 3), 3);
            buf_len_ = 3;
        }
        return false;
    }

    if (sync_pos > 0) {
        std::memmove(buffer_, buffer_ + sync_pos, buf_len_ - sync_pos);
        buf_len_ -= sync_pos;
    }

    if (buf_len_ < OGG_MIN_HEADER_SIZE) {
        return false;
    }

    // Parse header fields
    uint8_t seg_count = buffer_[26];
    size_t header_len = OGG_MIN_HEADER_SIZE + seg_count;
    if (buf_len_ < header_len) {
        return false;
    }

    // Calculate total body size from segment table
    size_t body_size = 0;
    for (size_t i = 0; i < seg_count; ++i) {
        body_size += buffer_[27 + i];
    }

    size_t total_page_len = header_len + body_size;
    if (buf_len_ < total_page_len) {
        return false; // Wait for full page data
    }

    // Verify CRC-32
    uint32_t original_crc = static_cast<uint32_t>(buffer_[22]) |
                           (static_cast<uint32_t>(buffer_[23]) << 8) |
                           (static_cast<uint32_t>(buffer_[24]) << 16) |
                           (static_cast<uint32_t>(buffer_[25]) << 24);

    // Zero out CRC bytes in buffer for calculation
    buffer_[22] = 0;
    buffer_[23] = 0;
    buffer_[24] = 0;
    buffer_[25] = 0;

    uint32_t calculated_crc = ogg_crc32(0, buffer_, total_page_len);

    // Restore CRC bytes
    buffer_[22] = static_cast<uint8_t>(original_crc & 0xFF);
    buffer_[23] = static_cast<uint8_t>((original_crc >> 8) & 0xFF);
    buffer_[24] = static_cast<uint8_t>((original_crc >> 16) & 0xFF);
    buffer_[25] = static_cast<uint8_t>((original_crc >> 24) & 0xFF);

    if (calculated_crc != original_crc) {
        // CRC mismatch: discard sync byte and resync
        std::memmove(buffer_, buffer_ + 1, buf_len_ - 1);
        buf_len_ -= 1;
        return false;
    }

    // Fill current header
    current_header_.stream_structure_version = buffer_[4];
    current_header_.header_type_flag = buffer_[5];
    
    // 64-bit granule pos little-endian
    uint64_t gran = 0;
    for (int b = 0; b < 8; ++b) {
        gran |= static_cast<uint64_t>(buffer_[6 + b]) << (b * 8);
    }
    current_header_.granule_position = static_cast<int64_t>(gran);

    current_header_.bitstream_serial_number = 
        static_cast<uint32_t>(buffer_[14]) | (static_cast<uint32_t>(buffer_[15]) << 8) |
        (static_cast<uint32_t>(buffer_[16]) << 16) | (static_cast<uint32_t>(buffer_[17]) << 24);

    current_header_.page_sequence_number = 
        static_cast<uint32_t>(buffer_[18]) | (static_cast<uint32_t>(buffer_[19]) << 8) |
        (static_cast<uint32_t>(buffer_[20]) << 16) | (static_cast<uint32_t>(buffer_[21]) << 24);

    current_header_.crc_checksum = original_crc;
    current_header_.page_segments = seg_count;
    std::memcpy(current_header_.segment_table, buffer_ + 27, seg_count);

    current_seg_index_ = 0;
    page_body_offset_ = header_len;
    page_total_size_ = total_page_len;
    in_page_ = true;

    return true;
}

bool OggDemuxer::assemble_packet() {
    if (!in_page_ || packet_ready_) {
        return false;
    }

    // Handle continuation flag at start of new page
    if (current_seg_index_ == 0) {
        bool is_cont = (current_header_.header_type_flag & OGG_FLAG_CONTINUED) != 0;
        if (!is_cont && packet_assembly_len_ > 0) {
            // Discontinuity: missing continued page; discard abandoned partial packet
            packet_assembly_len_ = 0;
            packet_is_bos_ = false;
        } else if (is_cont && packet_assembly_len_ == 0) {
            // Orphan continuation: skip segments until packet boundary
            while (current_seg_index_ < current_header_.page_segments) {
                uint8_t seg_len = current_header_.segment_table[current_seg_index_];
                page_body_offset_ += seg_len;
                current_seg_index_++;
                if (seg_len < 255) {
                    break;
                }
            }
        }
    }

    while (current_seg_index_ < current_header_.page_segments) {
        if (packet_assembly_len_ == 0) {
            if (current_seg_index_ == 0) {
                packet_is_bos_ = (current_header_.header_type_flag & OGG_FLAG_BOS) != 0;
            } else {
                packet_is_bos_ = false;
            }
        }

        uint8_t seg_len = current_header_.segment_table[current_seg_index_];
        
        if (packet_assembly_len_ + seg_len <= sizeof(packet_assembly_)) {
            if (seg_len > 0) {
                std::memcpy(packet_assembly_ + packet_assembly_len_, 
                            buffer_ + page_body_offset_, seg_len);
                packet_assembly_len_ += seg_len;
            }
        }

        page_body_offset_ += seg_len;
        current_seg_index_++;

        if (seg_len < 255) {
            // Packet boundary reached!
            packet_ready_ = true;
            bool is_last_seg = (current_seg_index_ >= current_header_.page_segments);
            if (is_last_seg) {
                packet_granule_pos_ = current_header_.granule_position;
                packet_is_eos_ = (current_header_.header_type_flag & OGG_FLAG_EOS) != 0;
            } else {
                packet_granule_pos_ = -1;
                packet_is_eos_ = false;
            }
            break;
        }
    }

    if (current_seg_index_ >= current_header_.page_segments) {
        // Page fully consumed, shift remaining buffer
        if (buf_len_ >= page_total_size_) {
            std::memmove(buffer_, buffer_ + page_total_size_, buf_len_ - page_total_size_);
            buf_len_ -= page_total_size_;
        } else {
            buf_len_ = 0;
        }
        in_page_ = false;
    }

    return packet_ready_;
}

int OggDemuxer::read_packet(uint8_t* out_packet, size_t max_out_bytes, 
                            int64_t& out_granule_pos, bool& out_is_bos, bool& out_is_eos) {
    if (!packet_ready_) {
        // Try to assemble if possible
        while (!packet_ready_) {
            if (!in_page_) {
                if (!parse_next_page()) break;
            }
            if (in_page_) {
                if (!assemble_packet()) break;
            }
        }
    }

    if (!packet_ready_) {
        return 0; // Need more data
    }

    if (packet_assembly_len_ > max_out_bytes) {
        return -1; // Buffer too small
    }

    if (out_packet && packet_assembly_len_ > 0) {
        std::memcpy(out_packet, packet_assembly_, packet_assembly_len_);
    }

    int pkt_size = static_cast<int>(packet_assembly_len_);
    out_granule_pos = packet_granule_pos_;
    out_is_bos = packet_is_bos_;
    out_is_eos = packet_is_eos_;

    // Reset assembly for next packet
    packet_assembly_len_ = 0;
    packet_ready_ = false;

    return pkt_size;
}

bool OggDemuxer::has_packet() const {
    if (packet_ready_) return true;
    OggDemuxer* mut_this = const_cast<OggDemuxer*>(this);
    while (!mut_this->packet_ready_) {
        if (!mut_this->in_page_) {
            if (!mut_this->parse_next_page()) break;
        }
        if (mut_this->in_page_) {
            if (!mut_this->assemble_packet()) break;
        }
    }
    return mut_this->packet_ready_;
}

} // namespace audio_codecs::ogg
