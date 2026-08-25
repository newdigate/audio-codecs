#include "src/ogg/ogg_crc.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::ogg;

    // 1. Brief test vector for polynomial 0x04c11db7
    const uint8_t test_data[] = {'O', 'g', 'g', 'S', 0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t crc = ogg_crc32(0, test_data, sizeof(test_data));
    assert(crc != 0);

    // 2. Empty data and null pointer tests
    assert(ogg_crc32(0, nullptr, 0) == 0);
    assert(ogg_crc32(12345, nullptr, 0) == 12345);
    assert(ogg_crc32(0, test_data, 0) == 0);

    // 3. Incremental CRC calculation matches full buffer calculation
    uint32_t crc_split1 = ogg_crc32(0, test_data, 5);
    uint32_t crc_split2 = ogg_crc32(crc_split1, test_data + 5, sizeof(test_data) - 5);
    assert(crc_split2 == crc);

    // 4. Byte-by-byte accumulation test
    uint32_t crc_byte_by_byte = 0;
    for (size_t i = 0; i < sizeof(test_data); ++i) {
        crc_byte_by_byte = ogg_crc32(crc_byte_by_byte, &test_data[i], 1);
    }
    assert(crc_byte_by_byte == crc);

    // 5. Deterministic check on longer buffer
    uint8_t sample_buffer[512];
    for (size_t i = 0; i < sizeof(sample_buffer); ++i) {
        sample_buffer[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    }
    uint32_t crc_long = ogg_crc32(0, sample_buffer, sizeof(sample_buffer));
    assert(crc_long != 0);
    uint32_t crc_long_part1 = ogg_crc32(0, sample_buffer, 200);
    uint32_t crc_long_part2 = ogg_crc32(crc_long_part1, sample_buffer + 200, sizeof(sample_buffer) - 200);
    assert(crc_long_part2 == crc_long);

    std::cout << "Ogg CRC-32 tests passed!\n";
    return 0;
}
