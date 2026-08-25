#pragma once

#include "audio_codecs/core/audio_types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_codecs::mp4 {

constexpr uint32_t make_fourcc(char a, char b, char c, char d) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(d));
}

// Standard ISOBMFF / MP4 Box FourCC constants
constexpr uint32_t FTYP = make_fourcc('f', 't', 'y', 'p');
constexpr uint32_t MOOV = make_fourcc('m', 'o', 'o', 'v');
constexpr uint32_t MVHD = make_fourcc('m', 'v', 'h', 'd');
constexpr uint32_t TRAK = make_fourcc('t', 'r', 'a', 'k');
constexpr uint32_t TKHD = make_fourcc('t', 'k', 'h', 'd');
constexpr uint32_t MDIA = make_fourcc('m', 'd', 'i', 'a');
constexpr uint32_t MDHD = make_fourcc('m', 'd', 'h', 'd');
constexpr uint32_t HDLR = make_fourcc('h', 'd', 'l', 'r');
constexpr uint32_t MINF = make_fourcc('m', 'i', 'n', 'f');
constexpr uint32_t SMHD = make_fourcc('s', 'm', 'h', 'd');
constexpr uint32_t DINF = make_fourcc('d', 'i', 'n', 'f');
constexpr uint32_t DREF = make_fourcc('d', 'r', 'e', 'f');
constexpr uint32_t STBL = make_fourcc('s', 't', 'b', 'l');
constexpr uint32_t STSD = make_fourcc('s', 't', 's', 'd');
constexpr uint32_t MP4A = make_fourcc('m', 'p', '4', 'a');
constexpr uint32_t ESDS = make_fourcc('e', 's', 'd', 's');
constexpr uint32_t STTS = make_fourcc('s', 't', 't', 's');
constexpr uint32_t STSZ = make_fourcc('s', 't', 's', 'z');
constexpr uint32_t STSC = make_fourcc('s', 't', 's', 'c');
constexpr uint32_t STCO = make_fourcc('s', 't', 'c', 'o');
constexpr uint32_t CO64 = make_fourcc('c', 'o', '6', '4');
constexpr uint32_t MDAT = make_fourcc('m', 'd', 'a', 't');
constexpr uint32_t FREE = make_fourcc('f', 'r', 'e', 'e');

// Additional brands and handler types
constexpr uint32_t BRAND_M4A  = make_fourcc('M', '4', 'A', ' ');
constexpr uint32_t BRAND_MP42 = make_fourcc('m', 'p', '4', '2');
constexpr uint32_t BRAND_ISOM = make_fourcc('i', 's', 'o', 'm');
constexpr uint32_t HANDLER_SOUN = make_fourcc('s', 'o', 'u', 'n');

struct AudioSpecificConfig {
    uint8_t audio_object_type{2};         // 2 = AAC-LC
    uint8_t sampling_frequency_index{4};  // 4 = 44100 Hz
    uint32_t sample_rate{44100};
    uint8_t channel_configuration{2};     // 2 = Stereo
};

std::vector<uint8_t> serialize_asc(const AudioSpecificConfig& asc);
bool parse_asc(const uint8_t* data, size_t size, AudioSpecificConfig& asc);

} // namespace audio_codecs::mp4
