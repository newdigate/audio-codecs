#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

struct AdtsHeader {
    uint16_t syncword{0xFFF};               // 12 bits: 0xFFF syncword
    uint8_t id{0};                          // 1 bit: 0=MPEG-4, 1=MPEG-2
    uint8_t layer{0};                       // 2 bits: '00' for AAC
    bool protection_absent{true};           // 1 bit: 1=no CRC, 0=CRC present
    uint8_t profile{1};                     // 2 bits: 1 = AAC-LC (MPEG-4 AudioObjectType - 1)
    uint8_t sampling_frequency_index{4};    // 4 bits: e.g. 4 for 44.1kHz, 3 for 48kHz
    uint32_t sample_rate{44100};            // Sample rate in Hz
    uint8_t private_bit{0};                 // 1 bit
    uint8_t channel_configuration{2};       // 3 bits: 1=mono, 2=stereo, etc.
    uint8_t original_copy{0};               // 1 bit
    uint8_t home{0};                        // 1 bit
    uint8_t copyright_identification_bit{0};      // 1 bit
    uint8_t copyright_identification_start{0};    // 1 bit
    uint16_t frame_length{0};               // 13 bits: total byte length including header + CRC + payload
    uint16_t adts_buffer_fullness{0x7FF};   // 11 bits: 0x7FF for variable rate bitstreams
    uint8_t num_raw_data_blocks{0};         // 2 bits: 0 means 1 AAC raw data block in frame
    uint16_t crc{0};                        // 16 bits if protection_absent == false

    size_t header_size_bytes() const { return protection_absent ? 7 : 9; }

    bool operator==(const AdtsHeader& o) const {
        return syncword == o.syncword &&
               id == o.id &&
               layer == o.layer &&
               protection_absent == o.protection_absent &&
               profile == o.profile &&
               sampling_frequency_index == o.sampling_frequency_index &&
               sample_rate == o.sample_rate &&
               private_bit == o.private_bit &&
               channel_configuration == o.channel_configuration &&
               original_copy == o.original_copy &&
               home == o.home &&
               copyright_identification_bit == o.copyright_identification_bit &&
               copyright_identification_start == o.copyright_identification_start &&
               frame_length == o.frame_length &&
               adts_buffer_fullness == o.adts_buffer_fullness &&
               num_raw_data_blocks == o.num_raw_data_blocks &&
               crc == o.crc;
    }

    bool operator!=(const AdtsHeader& o) const {
        return !(*this == o);
    }
};

} // namespace audio_codecs::aac
