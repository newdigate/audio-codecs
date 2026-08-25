// tests/test_flac_rice.cpp
#include "src/flac/decoder/rice_decoder.h"
#include "src/flac/encoder/rice_encoder.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;
    using namespace audio_codecs::core;

    int32_t orig[16] = {0, 1, -1, 2, -2, 5, -8, 12, -15, 0, 1, -1, 3, -4, 2, -2};

    // Test 4-bit Rice parameter encoding & decoding
    uint8_t param4 = RiceEncoder::find_optimal_rice_param(orig, 16, 4);

    uint8_t buffer[128] = {0};
    BitWriter writer;
    writer.init(buffer, sizeof(buffer));
    RiceEncoder::encode_residual_partition(writer, orig, 16, 4, param4);
    writer.flush_to_byte();

    BitReader reader;
    reader.init(buffer, writer.get_byte_count());

    int32_t decoded[16] = {0};
    bool ok = RiceDecoder::decode_residual_partition(reader, decoded, 16, 4);
    assert(ok);

    for (int i = 0; i < 16; ++i) {
        assert(decoded[i] == orig[i]);
    }

    // Test 5-bit Rice parameter encoding & decoding
    uint8_t param5 = RiceEncoder::find_optimal_rice_param(orig, 16, 5);

    std::memset(buffer, 0, sizeof(buffer));
    writer.init(buffer, sizeof(buffer));
    RiceEncoder::encode_residual_partition(writer, orig, 16, 5, param5);
    writer.flush_to_byte();

    reader.init(buffer, writer.get_byte_count());
    std::memset(decoded, 0, sizeof(decoded));
    ok = RiceDecoder::decode_residual_partition(reader, decoded, 16, 5);
    assert(ok);

    for (int i = 0; i < 16; ++i) {
        assert(decoded[i] == orig[i]);
    }

    // Test Escape Partition (parameter = 0b1111 for 4-bit)
    int32_t escape_data[8] = {-1000, 2000, -3000, 4000, -5000, 6000, -7000, 8000};
    std::memset(buffer, 0, sizeof(buffer));
    writer.init(buffer, sizeof(buffer));
    RiceEncoder::encode_escaped_partition(writer, escape_data, 8, 16);
    writer.flush_to_byte();

    reader.init(buffer, writer.get_byte_count());
    std::memset(decoded, 0, sizeof(decoded));
    ok = RiceDecoder::decode_residual_partition(reader, decoded, 8, 4);
    assert(ok);

    for (int i = 0; i < 8; ++i) {
        assert(decoded[i] == escape_data[i]);
    }

    std::cout << "FLAC Rice coding roundtrip passed!\n";
    return 0;
}
