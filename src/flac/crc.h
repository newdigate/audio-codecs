#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

// CRC-8 calculation (Polynomial 0x07, Initial 0x00) for FLAC frame headers
uint8_t crc8_update(uint8_t crc, uint8_t data);
uint8_t crc8_calculate(const uint8_t* data, size_t len);

// CRC-16-FLAC calculation (Polynomial 0x8005, Initial 0x0000) for FLAC frame footers
uint16_t crc16_update(uint16_t crc, uint8_t data);
uint16_t crc16_calculate(const uint8_t* data, size_t len);

} // namespace audio_codecs::flac
