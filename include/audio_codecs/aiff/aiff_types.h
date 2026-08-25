#pragma once
#include "audio_codecs/core/audio_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aiff {

enum class AiffFormType : uint32_t {
    Aiff = 0x41494646,  // 'AIFF'
    Aifc = 0x41494643   // 'AIFC'
};

enum class AiffCompressionType : uint32_t {
    None  = 0x4E4F4E45,  // 'NONE' - Big-endian uncompressed PCM
    Sowt  = 0x736F7774,  // 'sowt' - Little-endian uncompressed PCM ("twos" swapped)
    Fl32  = 0x666C3332,  // 'fl32' - 32-bit IEEE 754 floating point
    FL32  = 0x464C3332,  // 'FL32' - 32-bit IEEE float alternate
    ALaw  = 0x616C6177,  // 'alaw' - 8-bit ITU-T G.711 A-law
    MuLaw = 0x756C6177,  // 'ulaw' - 8-bit ITU-T G.711 µ-law
    In24  = 0x696E3234,  // 'in24' - 24-bit integer
    In32  = 0x696E3332   // 'in32' - 32-bit integer
};

enum class AiffSampleFormat {
    Int8,       // 8-bit signed integer (two's complement, range -128..127, 0 = silence)
    Int16BE,    // 16-bit signed Big-Endian integer
    Int24BE,    // 24-bit packed signed Big-Endian integer (3 bytes/sample)
    Int32BE,    // 32-bit signed Big-Endian integer
    Int16LE,    // 16-bit signed Little-Endian integer (sowt)
    Int24LE,    // 24-bit packed signed Little-Endian integer (sowt)
    Int32LE,    // 32-bit signed Little-Endian integer (sowt)
    Float32BE,  // 32-bit IEEE float Big-Endian
    Float32LE,  // 32-bit IEEE float Little-Endian
    ALaw8,      // 8-bit ITU-T G.711 A-law
    MuLaw8      // 8-bit ITU-T G.711 µ-law
};

struct AiffEncoderConfig {
    AudioConfig core_config{44100, 2, 0};
    AiffFormType form_type{AiffFormType::Aiff};
    AiffCompressionType compression_type{AiffCompressionType::None};
    uint8_t bits_per_sample{16};
    AiffSampleFormat sample_format{AiffSampleFormat::Int16BE};
};

} // namespace audio_codecs::aiff
