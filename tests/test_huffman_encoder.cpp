// tests/test_huffman_encoder.cpp
#include "src/mp3/encoder/huffman_encoder.h"
#include "src/mp3/encoder/quantizer.h"
#include "src/mp3/decoder/huffman_decoder.h"
#include "src/core/bit_reader.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;
    using namespace audio_codecs::core;

    // Test 1: Optimal table selection for small values [-1, 0, 1]
    int16_t is[10] = {0, 1, -1, 0, 1, 1, 0, 0, -1, 1};
    int table_num = HuffmanEncoder::choose_optimal_table(is, 10);
    assert(table_num >= 1 && table_num <= 3);

    // Test 2: Encode with HuffmanEncoder and decode with HuffmanDecoder
    uint8_t buffer[64] = {0};
    BitWriter writer;
    writer.init(buffer, sizeof(buffer));

    // Encode pairs
    HuffmanEncoder::encode_pairs(writer, is, 10, 1);
    writer.flush_to_byte();

    BitReader reader;
    reader.init(buffer, writer.get_byte_count());

    GranuleChannelInfo gi;
    gi.big_values = 5; // 5 pairs = 10 lines
    gi.table_select[0] = 1;
    gi.region0_count = 10;
    gi.region1_count = 0;
    gi.window_switching_flag = false;
    gi.block_type = 0;

    FrameHeader header;
    header.version = MpegVersion::Mpeg1;
    header.sample_rate = 44100;

    int16_t is_decoded[576] = {0};
    bool ok = HuffmanDecoder::decode_granule(reader, gi, header, is_decoded, writer.get_bit_count());
    assert(ok);

    for (int i = 0; i < 10; ++i) {
        assert(is_decoded[i] == is[i]);
    }

    std::cout << "Huffman encoder and roundtrip tests passed!\n";
    return 0;
}
