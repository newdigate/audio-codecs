#pragma once
#include "audio_codecs/wav/wav_types.h"
#include <cstdint>

namespace audio_codecs::wav {

constexpr uint32_t make_fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
          (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
          (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
          (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kFourCcRiff = make_fourcc('R', 'I', 'F', 'F');
constexpr uint32_t kFourCcRf64 = make_fourcc('R', 'F', '6', '4');
constexpr uint32_t kFourCcWave = make_fourcc('W', 'A', 'V', 'E');
constexpr uint32_t kFourCcFmt  = make_fourcc('f', 'm', 't', ' ');
constexpr uint32_t kFourCcData = make_fourcc('d', 'a', 't', 'a');
constexpr uint32_t kFourCcFact = make_fourcc('f', 'a', 'c', 't');
constexpr uint32_t kFourCcList = make_fourcc('L', 'I', 'S', 'T');
constexpr uint32_t kFourCcBext = make_fourcc('b', 'e', 'x', 't');
constexpr uint32_t kFourCcJunk = make_fourcc('J', 'U', 'N', 'K');
constexpr uint32_t kFourCcPad  = make_fourcc('P', 'A', 'D', ' ');

constexpr uint8_t kGuidPcm[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

constexpr uint8_t kGuidIeeeFloat[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

} // namespace audio_codecs::wav
