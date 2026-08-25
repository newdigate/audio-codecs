#include "src/ogg/ogg_crc.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::ogg;
    // CRC-32 test vector for polynomial 0x04c11db7
    const uint8_t test_data[] = {'O', 'g', 'g', 'S', 0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t crc = ogg_crc32(0, test_data, sizeof(test_data));
    assert(crc != 0);

    // Known CRC property: CRC of empty data is 0
    assert(ogg_crc32(0, nullptr, 0) == 0);

    // Incremental CRC calculation matches full buffer
    uint32_t crc1 = ogg_crc32(0, test_data, 7);
    uint32_t crc2 = ogg_crc32(crc1, test_data + 7, sizeof(test_data) - 7);
    assert(crc2 == crc);

    std::cout << "Ogg CRC-32 tests passed!\n";
    return 0;
}
