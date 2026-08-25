#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace audio_codecs::vorbis {

constexpr size_t VORBIS_MAX_CHANNELS = 2;
constexpr size_t VORBIS_MAX_BLOCK_SIZE = 8192;
constexpr size_t VORBIS_MAX_CODEBOOKS = 64;
constexpr size_t VORBIS_MAX_FLOORS = 8;
constexpr size_t VORBIS_MAX_RESIDUES = 8;
constexpr size_t VORBIS_MAX_MAPPINGS = 8;
constexpr size_t VORBIS_MAX_MODES = 64;

constexpr uint8_t VORBIS_PACKET_ID      = 0x01;
constexpr uint8_t VORBIS_PACKET_COMMENT = 0x03;
constexpr uint8_t VORBIS_PACKET_SETUP   = 0x05;

struct VorbisInfo {
    uint8_t channels{2};
    uint32_t sample_rate{44100};
    int32_t bitrate_maximum{0};
    int32_t bitrate_nominal{128000};
    int32_t bitrate_minimum{0};
    uint32_t blocksize_0{512};
    uint32_t blocksize_1{2048};
};

struct VorbisComment {
    std::string vendor{"audio_codecs vorbis v1.0.0"};
    std::vector<std::string> comments;
};

} // namespace audio_codecs::vorbis
