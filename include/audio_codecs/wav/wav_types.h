#pragma once
#include "audio_codecs/core/audio_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::wav {

enum class WavFormat : uint16_t {
    Pcm        = 0x0001,
    IeeeFloat  = 0x0003,
    ALaw       = 0x0006,
    MuLaw      = 0x0007,
    Extensible = 0xFFFE
};

enum class WavSampleFormat {
    Uint8,      // 8-bit unsigned integer (0..255, 128 = silence)
    Int16LE,    // 16-bit signed integer Little-Endian
    Int24LE,    // 24-bit packed signed integer Little-Endian (3 bytes/sample)
    Int32LE,    // 32-bit signed integer Little-Endian
    Float32LE,  // 32-bit IEEE float (-1.0f .. +1.0f)
    ALaw8,      // 8-bit companded A-law
    MuLaw8      // 8-bit companded µ-law
};

enum SpeakerMask : uint32_t {
    FrontLeft          = 0x00000001,
    FrontRight         = 0x00000002,
    FrontCenter        = 0x00000004,
    LowFrequency       = 0x00000008,
    BackLeft           = 0x00000010,
    BackRight          = 0x00000020,
    FrontLeftOfCenter  = 0x00000040,
    FrontRightOfCenter = 0x00000080,
    BackCenter         = 0x00000100,
    SideLeft           = 0x00000200,
    SideRight          = 0x00000400,
    StereoMask         = FrontLeft | FrontRight,
    Surround51Mask     = FrontLeft | FrontRight | FrontCenter | LowFrequency | BackLeft | BackRight
};

struct WavEncoderConfig {
    AudioConfig core_config{44100, 2, 0, false, 0};
    WavSampleFormat sample_format{WavSampleFormat::Int16LE};
    uint32_t channel_mask{0};
    bool use_extensible{false};
};

} // namespace audio_codecs::wav
