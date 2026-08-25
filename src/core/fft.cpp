#include "src/core/fft.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::core {

Fft1024::Fft1024() {
    init();
}

void Fft1024::init() {
    if (initialized_) return;

    // Precompute twiddle factors: exp(-2*pi*i*k/N)
    for (size_t i = 0; i < N / 2; ++i) {
        float angle = -constants::TWO_PI * static_cast<float>(i) / static_cast<float>(N);
        cos_table_[i] = std::cos(angle);
        sin_table_[i] = std::sin(angle);
    }

    // Precompute 10-bit bit-reversal table
    for (size_t i = 0; i < N; ++i) {
        uint16_t rev = 0;
        uint16_t val = static_cast<uint16_t>(i);
        for (int b = 0; b < 10; ++b) {
            rev = (rev << 1) | (val & 1);
            val >>= 1;
        }
        bit_rev_[i] = rev;
    }

    initialized_ = true;
}

void Fft1024::transform_real(const float* time_in, float* real_out, float* imag_out) {
    if (!initialized_) init();

    // Reorder according to bit-reversal table
    float xr[N];
    float xi[N];

    for (size_t i = 0; i < N; ++i) {
        uint16_t r = bit_rev_[i];
        xr[r] = time_in[i];
        xi[r] = 0.0f;
    }

    // Cooley-Tukey Radix-2 DIT FFT
    for (size_t len = 2; len <= N; len <<= 1) {
        size_t half_len = len >> 1;
        size_t step = N / len;

        for (size_t i = 0; i < N; i += len) {
            size_t k = 0;
            for (size_t j = 0; j < half_len; ++j) {
                float c = cos_table_[k];
                float s = sin_table_[k];

                float u_r = xr[i + j];
                float u_i = xi[i + j];
                float v_r = xr[i + j + half_len] * c - xi[i + j + half_len] * s;
                float v_i = xr[i + j + half_len] * s + xi[i + j + half_len] * c;

                xr[i + j] = u_r + v_r;
                xi[i + j] = u_i + v_i;
                xr[i + j + half_len] = u_r - v_r;
                xi[i + j + half_len] = u_i - v_i;

                k += step;
            }
        }
    }

    // Copy the first 513 bins (DC to Nyquist)
    for (size_t i = 0; i < NUM_BINS; ++i) {
        real_out[i] = xr[i];
        imag_out[i] = xi[i];
    }
}

} // namespace audio_codecs::core
