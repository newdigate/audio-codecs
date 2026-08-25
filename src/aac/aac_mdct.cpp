#include "src/aac/aac_mdct.h"
#include "src/aac/aac_tables.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace audio_codecs::aac {

namespace {

constexpr double PI = 3.14159265358979323846;

} // anonymous namespace

AacMdct::AacMdct() {
    init_tables();
}

void AacMdct::init_tables() {
    // 1. Long block FFT tables (M = 512, bits = 9)
    for (size_t i = 0; i < 512; ++i) {
        uint16_t rev = 0;
        uint16_t val = static_cast<uint16_t>(i);
        for (int b = 0; b < 9; ++b) {
            rev = (rev << 1) | (val & 1);
            val >>= 1;
        }
        bit_rev_512_[i] = rev;
    }

    for (size_t k = 0; k < 256; ++k) {
        double angle = -2.0 * PI * static_cast<double>(k) / 512.0;
        fft_twiddle_512_[k].re = static_cast<float>(std::cos(angle));
        fft_twiddle_512_[k].im = static_cast<float>(std::sin(angle));
    }

    for (size_t n = 0; n < 512; ++n) {
        double angle = -PI * static_cast<double>(n) / 1024.0;
        pre_twiddle_long_[n].re = static_cast<float>(std::cos(angle));
        pre_twiddle_long_[n].im = static_cast<float>(std::sin(angle));
    }

    for (size_t k = 0; k < 512; ++k) {
        double angle = -PI * (4.0 * static_cast<double>(k) + 1.0) / 4096.0;
        post_twiddle_long_[k].re = static_cast<float>(std::cos(angle));
        post_twiddle_long_[k].im = static_cast<float>(std::sin(angle));
    }

    // 2. Short block FFT tables (M = 64, bits = 6)
    for (size_t i = 0; i < 64; ++i) {
        uint16_t rev = 0;
        uint16_t val = static_cast<uint16_t>(i);
        for (int b = 0; b < 6; ++b) {
            rev = (rev << 1) | (val & 1);
            val >>= 1;
        }
        bit_rev_64_[i] = rev;
    }

    for (size_t k = 0; k < 32; ++k) {
        double angle = -2.0 * PI * static_cast<double>(k) / 64.0;
        fft_twiddle_64_[k].re = static_cast<float>(std::cos(angle));
        fft_twiddle_64_[k].im = static_cast<float>(std::sin(angle));
    }

    for (size_t n = 0; n < 64; ++n) {
        double angle = -PI * static_cast<double>(n) / 128.0;
        pre_twiddle_short_[n].re = static_cast<float>(std::cos(angle));
        pre_twiddle_short_[n].im = static_cast<float>(std::sin(angle));
    }

    for (size_t k = 0; k < 64; ++k) {
        double angle = -PI * (4.0 * static_cast<double>(k) + 1.0) / 512.0;
        post_twiddle_short_[k].re = static_cast<float>(std::cos(angle));
        post_twiddle_short_[k].im = static_cast<float>(std::sin(angle));
    }
}

void AacMdct::fft_512(Complex* data) const {
    // Bit-reversal permutation
    for (size_t i = 0; i < 512; ++i) {
        size_t r = bit_rev_512_[i];
        if (i < r) {
            std::swap(data[i], data[r]);
        }
    }

    // Cooley-Tukey Radix-2 DIT FFT
    for (size_t len = 2; len <= 512; len <<= 1) {
        size_t half_len = len >> 1;
        size_t step = 512 / len;
        for (size_t i = 0; i < 512; i += len) {
            size_t k = 0;
            for (size_t j = 0; j < half_len; ++j) {
                float c = fft_twiddle_512_[k].re;
                float s = fft_twiddle_512_[k].im;

                float u_r = data[i + j].re;
                float u_i = data[i + j].im;
                float v_r = data[i + j + half_len].re * c - data[i + j + half_len].im * s;
                float v_i = data[i + j + half_len].re * s + data[i + j + half_len].im * c;

                data[i + j].re = u_r + v_r;
                data[i + j].im = u_i + v_i;
                data[i + j + half_len].re = u_r - v_r;
                data[i + j + half_len].im = u_i - v_i;

                k += step;
            }
        }
    }
}

void AacMdct::fft_64(Complex* data) const {
    // Bit-reversal permutation
    for (size_t i = 0; i < 64; ++i) {
        size_t r = bit_rev_64_[i];
        if (i < r) {
            std::swap(data[i], data[r]);
        }
    }

    // Cooley-Tukey Radix-2 DIT FFT
    for (size_t len = 2; len <= 64; len <<= 1) {
        size_t half_len = len >> 1;
        size_t step = 64 / len;
        for (size_t i = 0; i < 64; i += len) {
            size_t k = 0;
            for (size_t j = 0; j < half_len; ++j) {
                float c = fft_twiddle_64_[k].re;
                float s = fft_twiddle_64_[k].im;

                float u_r = data[i + j].re;
                float u_i = data[i + j].im;
                float v_r = data[i + j + half_len].re * c - data[i + j + half_len].im * s;
                float v_i = data[i + j + half_len].re * s + data[i + j + half_len].im * c;

                data[i + j].re = u_r + v_r;
                data[i + j].im = u_i + v_i;
                data[i + j + half_len].re = u_r - v_r;
                data[i + j + half_len].im = u_i - v_i;

                k += step;
            }
        }
    }
}

void AacMdct::mdct_core_long(const float* in_time_2048, float* out_freq_1024) {
    if (!in_time_2048 || !out_freq_1024) return;

    // 1. Time folding: 2048 -> 1024
    float v[1024];
    for (size_t n = 0; n < 512; ++n) {
        v[n] = -in_time_2048[1536 + n] - in_time_2048[1536 - 1 - n];
        v[512 + n] = in_time_2048[n] - in_time_2048[1024 - 1 - n];
    }

    // 2. Pre-twiddle
    Complex c[512];
    for (size_t n = 0; n < 512; ++n) {
        float re = v[2 * n];
        float im = v[1024 - 1 - 2 * n];
        c[n].re = re * pre_twiddle_long_[n].re - im * pre_twiddle_long_[n].im;
        c[n].im = re * pre_twiddle_long_[n].im + im * pre_twiddle_long_[n].re;
    }

    // 3. 512-point FFT
    fft_512(c);

    // 4. Post-twiddle and unpack to 1024 frequency bins
    for (size_t k = 0; k < 512; ++k) {
        float w_re = c[k].re * post_twiddle_long_[k].re - c[k].im * post_twiddle_long_[k].im;
        float w_im = c[k].re * post_twiddle_long_[k].im + c[k].im * post_twiddle_long_[k].re;
        out_freq_1024[2 * k] = w_re;
        out_freq_1024[1024 - 1 - 2 * k] = -w_im;
    }
}

void AacMdct::imdct_core_long(const float* in_freq_1024, float* out_time_2048) {
    if (!in_freq_1024 || !out_time_2048) return;

    // 1. Pre-twiddle
    Complex c[512];
    for (size_t n = 0; n < 512; ++n) {
        float re = in_freq_1024[2 * n];
        float im = in_freq_1024[1024 - 1 - 2 * n];
        c[n].re = re * pre_twiddle_long_[n].re - im * pre_twiddle_long_[n].im;
        c[n].im = re * pre_twiddle_long_[n].im + im * pre_twiddle_long_[n].re;
    }

    // 2. 512-point FFT
    fft_512(c);

    // 3. Post-twiddle with scaling 2/N = 2/1024 = 1/512
    constexpr float scale = 2.0f / 1024.0f;
    float v[1024];
    for (size_t k = 0; k < 512; ++k) {
        float w_re = (c[k].re * post_twiddle_long_[k].re - c[k].im * post_twiddle_long_[k].im) * scale;
        float w_im = (c[k].re * post_twiddle_long_[k].im + c[k].im * post_twiddle_long_[k].re) * scale;
        v[2 * k] = w_re;
        v[1024 - 1 - 2 * k] = -w_im;
    }

    // 4. Time unfolding: 1024 -> 2048
    for (size_t n = 0; n < 512; ++n) {
        out_time_2048[n] = v[512 + n];
        out_time_2048[512 + n] = -v[1024 - 1 - n];
        out_time_2048[1024 + n] = -v[512 - 1 - n];
        out_time_2048[1536 + n] = -v[n];
    }
}

void AacMdct::mdct_core_short(const float* in_time_256, float* out_freq_128) {
    if (!in_time_256 || !out_freq_128) return;

    // 1. Time folding: 256 -> 128
    float v[128];
    for (size_t n = 0; n < 64; ++n) {
        v[n] = -in_time_256[192 + n] - in_time_256[192 - 1 - n];
        v[64 + n] = in_time_256[n] - in_time_256[128 - 1 - n];
    }

    // 2. Pre-twiddle
    Complex c[64];
    for (size_t n = 0; n < 64; ++n) {
        float re = v[2 * n];
        float im = v[128 - 1 - 2 * n];
        c[n].re = re * pre_twiddle_short_[n].re - im * pre_twiddle_short_[n].im;
        c[n].im = re * pre_twiddle_short_[n].im + im * pre_twiddle_short_[n].re;
    }

    // 3. 64-point FFT
    fft_64(c);

    // 4. Post-twiddle and unpack to 128 frequency bins
    for (size_t k = 0; k < 64; ++k) {
        float w_re = c[k].re * post_twiddle_short_[k].re - c[k].im * post_twiddle_short_[k].im;
        float w_im = c[k].re * post_twiddle_short_[k].im + c[k].im * post_twiddle_short_[k].re;
        out_freq_128[2 * k] = w_re;
        out_freq_128[128 - 1 - 2 * k] = -w_im;
    }
}

void AacMdct::imdct_core_short(const float* in_freq_128, float* out_time_256) {
    if (!in_freq_128 || !out_time_256) return;

    // 1. Pre-twiddle
    Complex c[64];
    for (size_t n = 0; n < 64; ++n) {
        float re = in_freq_128[2 * n];
        float im = in_freq_128[128 - 1 - 2 * n];
        c[n].re = re * pre_twiddle_short_[n].re - im * pre_twiddle_short_[n].im;
        c[n].im = re * pre_twiddle_short_[n].im + im * pre_twiddle_short_[n].re;
    }

    // 2. 64-point FFT
    fft_64(c);

    // 3. Post-twiddle with scaling 2/N = 2/128 = 1/64
    constexpr float scale = 2.0f / 128.0f;
    float v[128];
    for (size_t k = 0; k < 64; ++k) {
        float w_re = (c[k].re * post_twiddle_short_[k].re - c[k].im * post_twiddle_short_[k].im) * scale;
        float w_im = (c[k].re * post_twiddle_short_[k].im + c[k].im * post_twiddle_short_[k].re) * scale;
        v[2 * k] = w_re;
        v[128 - 1 - 2 * k] = -w_im;
    }

    // 4. Time unfolding: 128 -> 256
    for (size_t n = 0; n < 64; ++n) {
        out_time_256[n] = v[64 + n];
        out_time_256[64 + n] = -v[128 - 1 - n];
        out_time_256[128 + n] = -v[64 - 1 - n];
        out_time_256[192 + n] = -v[n];
    }
}

void AacMdct::forward_windowed(const float* in_time_2048, float* out_freq_1024,
                              WindowSequence seq, WindowShape shape_prev, WindowShape shape_curr) {
    if (!in_time_2048 || !out_freq_1024) return;

    if (seq == WindowSequence::EightShort) {
        size_t len = 0;
        const float* win0 = get_window(WindowSequence::EightShort, shape_prev, shape_curr, len);
        const float* winN = get_window(WindowSequence::EightShort, shape_curr, shape_curr, len);

        float windowed[256];
        // Short window 0 (uses shape_prev -> shape_curr transition)
        const float* in0 = &in_time_2048[448];
        for (size_t i = 0; i < 256; ++i) {
            windowed[i] = in0[i] * win0[i];
        }
        mdct_core_short(windowed, &out_freq_1024[0]);

        // Short windows 1..7 (use shape_curr -> shape_curr)
        for (size_t w = 1; w < 8; ++w) {
            const float* inW = &in_time_2048[448 + w * 128];
            for (size_t i = 0; i < 256; ++i) {
                windowed[i] = inW[i] * winN[i];
            }
            mdct_core_short(windowed, &out_freq_1024[w * 128]);
        }
    } else {
        size_t len = 0;
        const float* win = get_window(seq, shape_prev, shape_curr, len);
        float windowed[2048];
        for (size_t i = 0; i < 2048; ++i) {
            windowed[i] = in_time_2048[i] * win[i];
        }
        mdct_core_long(windowed, out_freq_1024);
    }
}

void AacMdct::inverse_windowed(const float* in_freq_1024, float* out_time_2048,
                              WindowSequence seq, WindowShape shape_prev, WindowShape shape_curr) {
    if (!in_freq_1024 || !out_time_2048) return;

    if (seq == WindowSequence::EightShort) {
        std::fill_n(out_time_2048, 2048, 0.0f);

        size_t len = 0;
        const float* win0 = get_window(WindowSequence::EightShort, shape_prev, shape_curr, len);
        const float* winN = get_window(WindowSequence::EightShort, shape_curr, shape_curr, len);

        float temp[256];
        // Short window 0
        imdct_core_short(&in_freq_1024[0], temp);
        for (size_t i = 0; i < 256; ++i) {
            out_time_2048[448 + i] += temp[i] * win0[i];
        }

        // Short windows 1..7
        for (size_t w = 1; w < 8; ++w) {
            imdct_core_short(&in_freq_1024[w * 128], temp);
            for (size_t i = 0; i < 256; ++i) {
                out_time_2048[448 + w * 128 + i] += temp[i] * winN[i];
            }
        }
    } else {
        imdct_core_long(in_freq_1024, out_time_2048);
        size_t len = 0;
        const float* win = get_window(seq, shape_prev, shape_curr, len);
        for (size_t i = 0; i < 2048; ++i) {
            out_time_2048[i] *= win[i];
        }
    }
}

void AacMdct::forward_long(const float* in_time_2048, float* out_freq_1024, WindowShape shape) {
    forward_windowed(in_time_2048, out_freq_1024, WindowSequence::OnlyLong, shape, shape);
}

void AacMdct::inverse_long(const float* in_freq_1024, float* out_time_2048, WindowShape shape) {
    inverse_windowed(in_freq_1024, out_time_2048, WindowSequence::OnlyLong, shape, shape);
}

void AacMdct::forward_short(const float* in_time_256, float* out_freq_128, WindowShape shape) {
    if (!in_time_256 || !out_freq_128) return;
    const float* win = (shape == WindowShape::Sine) ? get_sine_window_128() : get_kbd_window_128();
    float windowed[256];
    for (size_t i = 0; i < 256; ++i) {
        windowed[i] = in_time_256[i] * win[i];
    }
    mdct_core_short(windowed, out_freq_128);
}

void AacMdct::inverse_short(const float* in_freq_128, float* out_time_256, WindowShape shape) {
    if (!in_freq_128 || !out_time_256) return;
    imdct_core_short(in_freq_128, out_time_256);
    const float* win = (shape == WindowShape::Sine) ? get_sine_window_128() : get_kbd_window_128();
    for (size_t i = 0; i < 256; ++i) {
        out_time_256[i] *= win[i];
    }
}

void AacMdct::forward_eight_short(const float* in_time_2048, float* out_freq_1024, WindowShape shape) {
    forward_windowed(in_time_2048, out_freq_1024, WindowSequence::EightShort, shape, shape);
}

void AacMdct::inverse_eight_short(const float* in_freq_1024, float* out_time_2048, WindowShape shape) {
    inverse_windowed(in_freq_1024, out_time_2048, WindowSequence::EightShort, shape, shape);
}

} // namespace audio_codecs::aac
