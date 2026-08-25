// tests/test_huffman_decoder.cpp
#include "src/mp3/decoder/bit_reservoir.h"
#include "src/mp3/decoder/huffman_decoder.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;
    using namespace audio_codecs::core;

    // Test 1: Bit reservoir append and prepare
    BitReservoir reservoir;
    reservoir.reset();

    uint8_t frame1_data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    reservoir.append_main_data(frame1_data, 10);

    uint8_t frame2_data[10] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    reservoir.append_main_data(frame2_data, 10);

    // If main_data_begin is 4 bytes, we read starting from frame1 byte 6 (value 7)
    uint8_t scratch[64];
    BitReader reader;
    bool ok = reservoir.prepare_reader(4, 10, 8, reader, scratch);
    assert(ok);
    assert(reader.read_bits(8) == 7);
    assert(reader.read_bits(8) == 8);

    // Test 2: Huffman decoder with Table 1 (code 001 len 3 -> x=0, y=1, sign bit 0 -> +1)
    uint8_t huff_buf[32] = {0};
    BitWriter writer;
    writer.init(huff_buf, sizeof(huff_buf));

    // Encode pair (0, +1): code '001' (3 bits) + sign bit '0' (pos) = 0010 (4 bits = 0x20)
    writer.write_bits(0x01, 3); // code for (0, 1)
    writer.write_bits(0, 1);    // sign for y=+1 (0 = positive)
    writer.flush_to_byte();

    BitReader huff_reader;
    huff_reader.init(huff_buf, writer.get_byte_count());

    GranuleChannelInfo gi;
    gi.big_values = 1;
    gi.table_select[0] = 1;
    gi.region0_count = 1;
    gi.region1_count = 0;
    gi.window_switching_flag = false;
    gi.block_type = 0;

    FrameHeader header;
    header.version = MpegVersion::Mpeg1;
    header.sample_rate = 44100;

    int16_t is_out[576] = {0};
    ok = HuffmanDecoder::decode_granule(huff_reader, gi, header, is_out, 4);
    assert(ok);
    assert(is_out[0] == 0);
    assert(is_out[1] == 1);
    assert(is_out[2] == 0);
    assert(is_out[575] == 0);

    std::cout << "Bit reservoir and Huffman decoder tests passed!\n";
    return 0;
}
