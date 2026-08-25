#pragma once
#include "src/core/bit_writer.h"
#include "src/mp3/mp3_common.h"
#include "src/mp3/mp3_tables.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class HuffmanEncoder {
public:
    // Choose optimal Huffman table (0..31) for a sequence of spectral pairs
    static int choose_optimal_table(const int16_t* is, int len);

    // Calculate bit count required to encode pairs with a given table
    static size_t count_bits_pairs(const int16_t* is, int len, int table_num);

    // Encode pairs into bitstream using specified table
    static void encode_pairs(core::BitWriter& writer, const int16_t* is, int len, int table_num);

    // Encode count1 quadruples using Table A or Table B
    static void encode_count1(core::BitWriter& writer, const int16_t* is, int num_quads, bool table_b);

    // Calculate bit count required to encode quadruples
    static size_t count_bits_count1(const int16_t* is, int num_quads, bool table_b);
};

} // namespace audio_codecs::mp3
