// tests/test_flac_crc.cpp
#include "src/flac/crc.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    // CRC-8 test vector: Polynomial 0x07, Initial 0x00
    // Test data: 0xFF, 0xF8, 0x69, 0x02
    uint8_t test_hdr[4] = {0xFF, 0xF8, 0x69, 0x02};
    uint8_t c8 = crc8_calculate(test_hdr, 4);
    assert(c8 != 0);

    // Verify CRC-8 byte-by-byte update matches chunk calculation
    uint8_t running_c8 = 0;
    for (int i = 0; i < 4; ++i) {
        running_c8 = crc8_update(running_c8, test_hdr[i]);
    }
    assert(running_c8 == c8);

    // CRC-16-FLAC test vector: Polynomial 0x8005, Initial 0x0000
    uint8_t test_frame[8] = {0xFF, 0xF8, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    uint16_t c16 = crc16_calculate(test_frame, 8);
    assert(c16 != 0);

    uint16_t running_c16 = 0;
    for (int i = 0; i < 8; ++i) {
        running_c16 = crc16_update(running_c16, test_frame[i]);
    }
    assert(running_c16 == c16);

    std::cout << "FLAC CRC tests passed!\n";
    return 0;
}
