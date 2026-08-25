#pragma once

#include "include/audio_codecs/aac/adts_header.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

// Parse ADTS header from a BitReader.
// Returns true on success, false if bitstream is invalid or truncated.
bool parse_adts_header(core::BitReader& reader, AdtsHeader& header);

// Serialize ADTS header to a BitWriter.
// Returns number of bytes written (7 for standard header, 9 for CRC protected header).
size_t write_adts_header(core::BitWriter& writer, const AdtsHeader& header);

// Calculate ADTS CRC-16 (polynomial 0x8005, initial 0xFFFF).
// If frame_data represents an ADTS frame with protection_absent == false,
// the 16-bit CRC field itself (bytes 7-8) is excluded from calculation.
uint16_t calculate_adts_crc(const uint8_t* frame_data, size_t total_frame_bytes);

// Scan a byte buffer for the first valid ADTS frame syncword.
// If found, writes byte index to offset and returns true. Otherwise returns false.
bool find_adts_sync(const uint8_t* data, size_t size, size_t& offset);

} // namespace audio_codecs::aac
