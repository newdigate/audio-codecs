#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs {

enum class SampleFormat {
    Float32,
    Int16
};

enum class ChannelMode {
    Mono = 1,
    Stereo = 2
};

struct AudioConfig {
    uint32_t sample_rate{44100};
    uint8_t channels{2};
    uint32_t bitrate_kbps{128};
    bool vbr{false};
    uint8_t vbr_quality{4}; // 0 (highest) to 9 (lowest)
};

template <typename T>
struct PcmView {
    T* data{nullptr};
    size_t samples_per_channel{0};
    uint8_t channels{2};
    bool interleaved{true};
};

} // namespace audio_codecs
