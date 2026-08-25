// tests/test_bitstream.cpp
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::core;
    uint8_t buffer[16] = {0};
    BitWriter writer;
    writer.init(buffer, sizeof(buffer));
    
    // Write 11-bit syncword 0x7FF, 2-bit layer 0x01, 1-bit protection 0x01, 4-bit bitrate 0x09
    writer.write_bits(0x7FF, 11);
    writer.write_bits(0x01, 2);
    writer.write_bits(0x01, 1);
    writer.write_bits(0x09, 4);

    assert(writer.get_bit_count() == 18);
    writer.flush_to_byte();
    assert(writer.get_byte_count() == 3);

    BitReader reader;
    reader.init(buffer, writer.get_byte_count());
    assert(reader.read_bits(11) == 0x7FF);
    assert(reader.read_bits(2) == 0x01);
    assert(reader.read_bits(1) == 0x01);
    assert(reader.read_bits(4) == 0x09);
    assert(reader.bits_remaining() == 6);

    // Test peek & skip
    reader.init(buffer, writer.get_byte_count());
    assert(reader.peek_bits(11) == 0x7FF);
    assert(reader.peek_bits(11) == 0x7FF); // peek should not advance
    reader.skip_bits(11);
    assert(reader.read_bits(2) == 0x01);

    // Test repositioning
    reader.set_position_bits(0);
    assert(reader.read_bits(11) == 0x7FF);

    std::cout << "Bitstream tests passed!\n";
    return 0;
}
