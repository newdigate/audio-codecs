#include "src/ogg/ogg_crc.h"
#include <array>

namespace audio_codecs::ogg {

namespace {

constexpr std::array<uint32_t, 256> generate_crc_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t r = i << 24;
        for (int j = 0; j < 8; ++j) {
            if (r & 0x80000000U) {
                r = (r << 1) ^ 0x04C11DB7U;
            } else {
                r <<= 1;
            }
        }
        table[i] = r;
    }
    return table;
}

constexpr auto kOggCrcTable = generate_crc_table();

} // namespace

uint32_t ogg_crc32(uint32_t crc, const uint8_t* data, size_t len) {
    if (!data) return crc;
    for (size_t i = 0; i < len; ++i) {
        crc = (crc << 8) ^ kOggCrcTable[((crc >> 24) & 0xFFU) ^ data[i]];
    }
    return crc;
}

} // namespace audio_codecs::ogg
