#include "src/mp3/decoder/huffman_decoder.h"
#include <cstring>
#include <algorithm>

namespace audio_codecs::mp3 {

bool HuffmanDecoder::decode_pair(core::BitReader& reader, 
                                 const HuffmanCodebook& book, 
                                 int16_t& x_out, 
                                 int16_t& y_out) {
    if (book.table_num == 0 || book.num_entries == 0 || !book.entries) {
        x_out = 0;
        y_out = 0;
        return true;
    }

    // Match code prefix
    // Max code length in MP3 Huffman tables is 19 bits
    uint32_t peek_word = reader.peek_bits(19);
    bool found = false;
    uint8_t x = 0, y = 0, len = 0;

    for (size_t i = 0; i < book.num_entries; ++i) {
        const HuffmanEntry& e = book.entries[i];
        if (e.len == 0) continue;
        uint32_t candidate = (peek_word >> (19 - e.len));
        if (candidate == e.code) {
            x = e.x;
            y = e.y;
            len = e.len;
            found = true;
            break;
        }
    }

    if (!found) {
        return false;
    }

    reader.skip_bits(len);

    int16_t val_x = x;
    int16_t val_y = y;

    // Linbits extension for x
    if (x == 15 && book.linbits > 0) {
        val_x += static_cast<int16_t>(reader.read_bits(book.linbits));
    }
    // Sign bit for x
    if (val_x != 0) {
        uint32_t sign_x = reader.read_bits(1);
        if (sign_x != 0) val_x = -val_x;
    }

    // Linbits extension for y
    if (y == 15 && book.linbits > 0) {
        val_y += static_cast<int16_t>(reader.read_bits(book.linbits));
    }
    // Sign bit for y
    if (val_y != 0) {
        uint32_t sign_y = reader.read_bits(1);
        if (sign_y != 0) val_y = -val_y;
    }

    x_out = val_x;
    y_out = val_y;
    return true;
}

bool HuffmanDecoder::decode_quad(core::BitReader& reader, 
                                 bool table_b, 
                                 int16_t& v_out, 
                                 int16_t& w_out, 
                                 int16_t& x_out, 
                                 int16_t& y_out) {
    uint8_t v = 0, w = 0, x = 0, y = 0, len = 0;

    if (!table_b) {
        // Table A (variable length 1..6 bits)
        uint32_t peek_word = reader.peek_bits(6);
        bool found = false;
        for (int i = 0; i < 16; ++i) {
            const Count1Entry& e = COUNT1_TABLE_A[i];
            uint32_t candidate = (peek_word >> (6 - e.len));
            if (candidate == e.code) {
                v = e.v; w = e.w; x = e.x; y = e.y;
                len = e.len;
                found = true;
                break;
            }
        }
        if (!found) return false;
        reader.skip_bits(len);
    } else {
        // Table B (4 bits fixed: 0 -> 1, 1 -> 0)
        uint32_t val = reader.read_bits(4);
        v = (val & 8) ? 0 : 1;
        w = (val & 4) ? 0 : 1;
        x = (val & 2) ? 0 : 1;
        y = (val & 1) ? 0 : 1;
    }

    // Sign bits
    int16_t val_v = v;
    int16_t val_w = w;
    int16_t val_x = x;
    int16_t val_y = y;

    if (val_v != 0 && reader.read_bits(1) != 0) val_v = -val_v;
    if (val_w != 0 && reader.read_bits(1) != 0) val_w = -val_w;
    if (val_x != 0 && reader.read_bits(1) != 0) val_x = -val_x;
    if (val_y != 0 && reader.read_bits(1) != 0) val_y = -val_y;

    v_out = val_v;
    w_out = val_w;
    x_out = val_x;
    y_out = val_y;
    return true;
}

bool HuffmanDecoder::decode_granule(core::BitReader& reader, 
                                   const GranuleChannelInfo& gi, 
                                   const FrameHeader& header, 
                                   int16_t* is_out_576, 
                                   size_t part3_bits) {
    if (!is_out_576) return false;
    std::memset(is_out_576, 0, 576 * sizeof(int16_t));

    size_t start_pos = reader.get_position_bits();
    size_t end_pos = start_pos + part3_bits;

    // Determine region boundaries
    size_t region0_limit = 0;
    size_t region1_limit = 0;
    size_t big_values_limit = std::min(static_cast<size_t>(gi.big_values * 2), static_cast<size_t>(576));

    const uint16_t* sfb_table = get_scalefac_band_table_long(header.sample_rate);

    if (gi.window_switching_flag && (gi.block_type == 2)) {
        // Short blocks: 3 windows
        region0_limit = 36; // scalefactor band 3 (3 * 12 = 36)
        region1_limit = 576;
    } else {
        // Long blocks
        int r0 = gi.region0_count + 1;
        int r1 = r0 + gi.region1_count + 1;
        r0 = std::min(r0, 22);
        r1 = std::min(r1, 22);
        region0_limit = sfb_table[r0];
        region1_limit = sfb_table[r1];
    }

    region0_limit = std::min(region0_limit, big_values_limit);
    region1_limit = std::min(region1_limit, big_values_limit);

    size_t l = 0;

    // 1. Big Values Region 0
    const HuffmanCodebook& book0 = HUFFMAN_CODEBOOKS[gi.table_select[0]];
    while (l < region0_limit && reader.get_position_bits() < end_pos) {
        if (!decode_pair(reader, book0, is_out_576[l], is_out_576[l + 1])) {
            break;
        }
        l += 2;
    }

    // 2. Big Values Region 1
    const HuffmanCodebook& book1 = HUFFMAN_CODEBOOKS[gi.table_select[1]];
    while (l < region1_limit && reader.get_position_bits() < end_pos) {
        if (!decode_pair(reader, book1, is_out_576[l], is_out_576[l + 1])) {
            break;
        }
        l += 2;
    }

    // 3. Big Values Region 2
    const HuffmanCodebook& book2 = HUFFMAN_CODEBOOKS[gi.table_select[2]];
    while (l < big_values_limit && reader.get_position_bits() < end_pos) {
        if (!decode_pair(reader, book2, is_out_576[l], is_out_576[l + 1])) {
            break;
        }
        l += 2;
    }

    // 4. Count1 quadruples
    bool table_b = gi.count1table_select;
    while (l + 3 < 576 && reader.get_position_bits() < end_pos) {
        if (!decode_quad(reader, table_b, 
                         is_out_576[l], is_out_576[l + 1], 
                         is_out_576[l + 2], is_out_576[l + 3])) {
            break;
        }
        l += 4;
    }

    // Skip any stuffing / unread bits in part3
    if (reader.get_position_bits() < end_pos) {
        reader.set_position_bits(end_pos);
    }

    return true;
}

} // namespace audio_codecs::mp3
