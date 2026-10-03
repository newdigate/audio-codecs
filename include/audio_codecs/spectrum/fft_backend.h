#pragma once
#include <cstdint>
#include <cstddef>

namespace audio_codecs::spectrum {

constexpr size_t FFT_SIZE = 1024;
constexpr size_t NUM_MAG_BINS = FFT_SIZE / 2; // 512 bins (DC to Nyquist)

class FftBackend {
public:
    virtual ~FftBackend() = default;
    virtual void forward_1024(const int16_t* in_pcm, float* out_magnitudes_512) = 0;
};

using FftTransformFn = void (*)(const int16_t* in_pcm, float* out_magnitudes_512, void* user_data);

} // namespace audio_codecs::spectrum
