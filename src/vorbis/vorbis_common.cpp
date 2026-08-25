#include "src/vorbis/vorbis_common.h"
#include <array>
#include <cmath>
#include <cstring>

namespace audio_codecs::vorbis {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::array<float, 256> generate_floor1_table() {
    std::array<float, 256> table{};
    for (size_t i = 0; i < 256; ++i) {
        table[i] = static_cast<float>(std::pow(10.0, 7.0 * (static_cast<double>(i) / 255.0 - 1.0)));
    }
    return table;
}

const auto kFloor1Table = generate_floor1_table();

} // namespace

const float kFloor1InverseDbTable[256] = {
    // Computed table initialized from kFloor1Table
};

float vorbis_unpack_float32(uint32_t val) {
    int32_t mantissa = static_cast<int32_t>(val & 0x1FFFFFU);
    uint32_t sign = val & 0x80000000U;
    int32_t exponent = static_cast<int32_t>((val & 0x7FE00000U) >> 21);
    if (sign) {
        mantissa = -mantissa;
    }
    return std::ldexp(static_cast<float>(mantissa), exponent - 788);
}

uint32_t vorbis_pack_float32(float val) {
    if (val == 0.0f) return 0;
    uint32_t sign = 0;
    if (val < 0.0f) {
        sign = 0x80000000U;
        val = -val;
    }
    int exp = 0;
    float norm = std::frexp(val, &exp);
    int32_t mantissa = static_cast<int32_t>(std::round(norm * static_cast<float>(1 << 20)));
    int32_t vorbis_exp = exp - 20 + 788;
    if (vorbis_exp < 0) vorbis_exp = 0;
    if (vorbis_exp > 1023) vorbis_exp = 1023;
    return sign | (static_cast<uint32_t>(vorbis_exp & 0x3FF) << 21) | (static_cast<uint32_t>(mantissa & 0x1FFFFF));
}

void vorbis_generate_window(float* out_window, size_t n) {
    if (!out_window || n == 0) return;
    for (size_t i = 0; i < n; ++i) {
        double arg = (static_cast<double>(i) + 0.5) / static_cast<double>(n) * (kPi * 0.5);
        double s = std::sin(arg);
        out_window[i] = static_cast<float>(std::sin(0.5 * kPi * s * s));
    }
}

void vorbis_generate_slope_window(float* out_window, size_t n_curr, size_t n_prev, size_t n_next) {
    if (!out_window || n_curr == 0) return;

    // Default symmetric window if sizes match
    if (n_curr == n_prev && n_curr == n_next) {
        vorbis_generate_window(out_window, n_curr);
        return;
    }

    std::memset(out_window, 0, n_curr * sizeof(float));

    // Left half
    if (n_curr > n_prev) {
        size_t pad = (n_curr - n_prev) / 4;
        for (size_t i = 0; i < pad; ++i) {
            out_window[i] = 0.0f;
        }
        for (size_t i = 0; i < n_prev / 2; ++i) {
            double arg = (static_cast<double>(i) + 0.5) / static_cast<double>(n_prev) * (kPi * 0.5);
            double s = std::sin(arg);
            out_window[pad + i] = static_cast<float>(std::sin(0.5 * kPi * s * s));
        }
        for (size_t i = pad + n_prev / 2; i < n_curr / 2; ++i) {
            out_window[i] = 1.0f;
        }
    } else {
        for (size_t i = 0; i < n_curr / 2; ++i) {
            double arg = (static_cast<double>(i) + 0.5) / static_cast<double>(n_curr) * (kPi * 0.5);
            double s = std::sin(arg);
            out_window[i] = static_cast<float>(std::sin(0.5 * kPi * s * s));
        }
    }

    // Right half
    if (n_curr > n_next) {
        size_t pad = (n_curr - n_next) / 4;
        for (size_t i = n_curr / 2; i < n_curr - pad - n_next / 2; ++i) {
            out_window[i] = 1.0f;
        }
        for (size_t i = 0; i < n_next / 2; ++i) {
            double arg = (static_cast<double>(n_next / 2 + i) + 0.5) / static_cast<double>(n_next) * (kPi * 0.5);
            double s = std::sin(arg);
            out_window[n_curr - pad - n_next / 2 + i] = static_cast<float>(std::sin(0.5 * kPi * s * s));
        }
        for (size_t i = n_curr - pad; i < n_curr; ++i) {
            out_window[i] = 0.0f;
        }
    } else {
        for (size_t i = n_curr / 2; i < n_curr; ++i) {
            double arg = (static_cast<double>(i) + 0.5) / static_cast<double>(n_curr) * (kPi * 0.5);
            double s = std::sin(arg);
            out_window[i] = static_cast<float>(std::sin(0.5 * kPi * s * s));
        }
    }
}

} // namespace audio_codecs::vorbis
