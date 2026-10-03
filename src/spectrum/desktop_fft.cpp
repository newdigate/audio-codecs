#include "audio_codecs/spectrum/desktop_fft.h"
#include <cmath>

namespace audio_codecs::spectrum {

DesktopRealFftBackend::DesktopRealFftBackend() {
    init();
}

bool DesktopRealFftBackend::init() {
    if (initialized_) return true;

    // Bit-reversal table for 1024 points (10 bits)
    for (uint32_t i = 0; i < 1024; ++i) {
        uint32_t rev = 0;
        uint32_t temp = i;
        for (int j = 0; j < 10; ++j) {
            rev = (rev << 1) | (temp & 1);
            temp >>= 1;
        }
        bit_reverse_[i] = static_cast<uint16_t>(rev);
    }

    // Twiddle factors for N=1024
    constexpr float kPi = 3.14159265358979323846f;
    for (size_t i = 0; i < 512; ++i) {
        float angle = -2.0f * kPi * static_cast<float>(i) / 1024.0f;
        twiddle_cos_[i] = std::cos(angle);
        twiddle_sin_[i] = std::sin(angle);
    }

    initialized_ = true;
    return true;
}

void DesktopRealFftBackend::forward_1024(const int16_t* in_pcm, float* out_magnitudes_512) {
    if (!initialized_) init();

    float real[1024];
    float imag[1024];

    // Bit-reverse reordering and int16 to float
    for (size_t i = 0; i < 1024; ++i) {
        uint16_t rev = bit_reverse_[i];
        real[i] = static_cast<float>(in_pcm[rev]);
        imag[i] = 0.0f;
    }

    // Cooley-Tukey Radix-2 FFT
    for (size_t half_size = 1; half_size < 1024; half_size <<= 1) {
        size_t step = half_size << 1;
        size_t twiddle_step = 512 / half_size;

        for (size_t k = 0; k < 1024; k += step) {
            for (size_t j = 0; j < half_size; ++j) {
                size_t tw_idx = j * twiddle_step;
                float c = twiddle_cos_[tw_idx];
                float s = twiddle_sin_[tw_idx];

                size_t u = k + j;
                size_t v = u + half_size;

                float tr = real[v] * c - imag[v] * s;
                float ti = real[v] * s + imag[v] * c;

                real[v] = real[u] - tr;
                imag[v] = imag[u] - ti;
                real[u] += tr;
                imag[u] += ti;
            }
        }
    }

    // Output 512 magnitude bins
    for (size_t i = 0; i < 512; ++i) {
        out_magnitudes_512[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
    }
}

} // namespace audio_codecs::spectrum
