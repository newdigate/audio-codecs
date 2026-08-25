#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::ogg {

// Calculate 32-bit CRC according to RFC 3533 (generator polynomial 0x04C11DB7)
uint32_t ogg_crc32(uint32_t crc, const uint8_t* data, size_t len);

} // namespace audio_codecs::ogg
