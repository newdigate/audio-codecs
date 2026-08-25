#include "src/flac/metadata.h"
#include <cstring>

namespace audio_codecs::flac {

bool MetadataParser::parse_streaminfo_payload(const uint8_t* in, size_t len, FlacStreamInfo& out_info) {
    if (!in || len < 34) return false;

    out_info.min_block_size = (static_cast<uint16_t>(in[0]) << 8) | in[1];
    out_info.max_block_size = (static_cast<uint16_t>(in[2]) << 8) | in[3];
    out_info.min_frame_size = (static_cast<uint32_t>(in[4]) << 16) | (static_cast<uint32_t>(in[5]) << 8) | in[6];
    out_info.max_frame_size = (static_cast<uint32_t>(in[7]) << 16) | (static_cast<uint32_t>(in[8]) << 8) | in[9];

    // Sample rate: 20 bits (in[10], in[11], high 4 bits of in[12])
    out_info.sample_rate = (static_cast<uint32_t>(in[10]) << 12) |
                           (static_cast<uint32_t>(in[11]) << 4)  |
                           (static_cast<uint32_t>(in[12] >> 4));

    // Channels: 3 bits (in[12] bits 3..1) + 1
    out_info.channels = ((in[12] >> 1) & 0x07) + 1;

    // Bits per sample: 5 bits (in[12] bit 0, in[13] bits 7..4) + 1
    out_info.bits_per_sample = (((in[12] & 0x01) << 4) | (in[13] >> 4)) + 1;

    // Total samples: 36 bits (low 4 bits of in[13], in[14..17])
    out_info.total_samples = (static_cast<uint64_t>(in[13] & 0x0F) << 32) |
                             (static_cast<uint64_t>(in[14]) << 24)        |
                             (static_cast<uint64_t>(in[15]) << 16)        |
                             (static_cast<uint64_t>(in[16]) << 8)         |
                             static_cast<uint64_t>(in[17]);

    std::memcpy(out_info.md5_signature, &in[18], 16);
    return true;
}

bool MetadataParser::parse_stream_header(const uint8_t* in, size_t len, FlacStreamInfo& out_info, size_t& bytes_consumed) {
    bytes_consumed = 0;
    if (!in || len < 42) return false;

    // Check magic "fLaC"
    if (in[0] != 'f' || in[1] != 'L' || in[2] != 'a' || in[3] != 'C') {
        return false;
    }

    size_t offset = 4;
    bool found_streaminfo = false;

    while (offset + 4 <= len) {
        bool is_last = (in[offset] & 0x80) != 0;
        uint8_t block_type = in[offset] & 0x7F;
        uint32_t block_len = (static_cast<uint32_t>(in[offset + 1]) << 16) |
                             (static_cast<uint32_t>(in[offset + 2]) << 8)  |
                             in[offset + 3];

        offset += 4;
        if (offset + block_len > len) {
            return false; // Incomplete block
        }

        if (block_type == static_cast<uint8_t>(FlacMetadataType::StreamInfo)) {
            if (block_len >= 34) {
                parse_streaminfo_payload(&in[offset], block_len, out_info);
                found_streaminfo = true;
            }
        }

        offset += block_len;
        if (is_last) {
            break;
        }
    }

    if (!found_streaminfo) return false;

    bytes_consumed = offset;
    return true;
}

size_t MetadataBuilder::write_stream_header(uint8_t* out, size_t max_len, const FlacStreamInfo& info, bool is_last) {
    if (!out || max_len < 42) return 0;

    // "fLaC"
    out[0] = 'f';
    out[1] = 'L';
    out[2] = 'a';
    out[3] = 'C';

    // Metadata block header (STREAMINFO)
    out[4] = (is_last ? 0x80 : 0x00) | static_cast<uint8_t>(FlacMetadataType::StreamInfo);
    out[5] = 0x00;
    out[6] = 0x00;
    out[7] = 34; // 34 bytes payload

    // Min / max block size (16 bits each)
    out[8] = static_cast<uint8_t>(info.min_block_size >> 8);
    out[9] = static_cast<uint8_t>(info.min_block_size & 0xFF);
    out[10] = static_cast<uint8_t>(info.max_block_size >> 8);
    out[11] = static_cast<uint8_t>(info.max_block_size & 0xFF);

    // Min / max frame size (24 bits each)
    out[12] = static_cast<uint8_t>((info.min_frame_size >> 16) & 0xFF);
    out[13] = static_cast<uint8_t>((info.min_frame_size >> 8) & 0xFF);
    out[14] = static_cast<uint8_t>(info.min_frame_size & 0xFF);
    out[15] = static_cast<uint8_t>((info.max_frame_size >> 16) & 0xFF);
    out[16] = static_cast<uint8_t>((info.max_frame_size >> 8) & 0xFF);
    out[17] = static_cast<uint8_t>(info.max_frame_size & 0xFF);

    // Sample rate (20 bits)
    out[18] = static_cast<uint8_t>((info.sample_rate >> 12) & 0xFF);
    out[19] = static_cast<uint8_t>((info.sample_rate >> 4) & 0xFF);
    
    // Low 4 bits of sample rate + Channels (3 bits: ch-1) + High 1 bit of (bps-1)
    uint8_t ch_code = (info.channels > 0) ? (info.channels - 1) & 0x07 : 0;
    uint8_t bps_code = (info.bits_per_sample > 0) ? (info.bits_per_sample - 1) & 0x1F : 0;

    out[20] = static_cast<uint8_t>(((info.sample_rate & 0x0F) << 4) | (ch_code << 1) | ((bps_code >> 4) & 0x01));
    
    // Low 4 bits of (bps-1) + High 4 bits of total_samples
    out[21] = static_cast<uint8_t>(((bps_code & 0x0F) << 4) | ((info.total_samples >> 32) & 0x0F));
    out[22] = static_cast<uint8_t>((info.total_samples >> 24) & 0xFF);
    out[23] = static_cast<uint8_t>((info.total_samples >> 16) & 0xFF);
    out[24] = static_cast<uint8_t>((info.total_samples >> 8) & 0xFF);
    out[25] = static_cast<uint8_t>(info.total_samples & 0xFF);

    std::memcpy(&out[26], info.md5_signature, 16);

    return 42;
}

bool MetadataBuilder::update_streaminfo(uint8_t* streaminfo_header_ptr, uint64_t total_samples, const uint8_t md5[16]) {
    if (!streaminfo_header_ptr) return false;

    // Check for "fLaC"
    if (streaminfo_header_ptr[0] == 'f' && streaminfo_header_ptr[1] == 'L' &&
        streaminfo_header_ptr[2] == 'a' && streaminfo_header_ptr[3] == 'C') {
        
        // Offset 21 contains low 4 bits of bps-1 in high nibble, and high 4 bits of total_samples in low nibble
        streaminfo_header_ptr[21] = (streaminfo_header_ptr[21] & 0xF0) | static_cast<uint8_t>((total_samples >> 32) & 0x0F);
        streaminfo_header_ptr[22] = static_cast<uint8_t>((total_samples >> 24) & 0xFF);
        streaminfo_header_ptr[23] = static_cast<uint8_t>((total_samples >> 16) & 0xFF);
        streaminfo_header_ptr[24] = static_cast<uint8_t>((total_samples >> 8) & 0xFF);
        streaminfo_header_ptr[25] = static_cast<uint8_t>(total_samples & 0xFF);

        if (md5) {
            std::memcpy(&streaminfo_header_ptr[26], md5, 16);
        }
        return true;
    }
    return false;
}

} // namespace audio_codecs::flac
