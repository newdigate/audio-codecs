#include "src/aiff/ieee80.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::aiff {

void uint32_to_ieee80(uint32_t sample_rate, uint8_t out[10]) {
    std::memset(out, 0, 10);
    if (sample_rate == 0) return;

    int k = 31;
    while (k >= 0 && ((sample_rate & (1u << k)) == 0)) {
        --k;
    }
    if (k < 0) return;

    uint16_t exp = static_cast<uint16_t>(16383 + k);
    uint64_t mant = static_cast<uint64_t>(sample_rate) << (63 - k);

    out[0] = static_cast<uint8_t>((exp >> 8) & 0x7F);
    out[1] = static_cast<uint8_t>(exp & 0xFF);
    for (int i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<uint8_t>((mant >> (56 - 8 * i)) & 0xFF);
    }
}

void double_to_ieee80(double value, uint8_t out[10]) {
    std::memset(out, 0, 10);
    if (value == 0.0) return;

    uint8_t sign = 0;
    if (value < 0.0) {
        sign = 1;
        value = -value;
    }

    int exp = 0;
    double fmant = std::frexp(value, &exp);
    --exp;
    fmant *= 2.0;

    uint16_t ieee_exp = static_cast<uint16_t>(16383 + exp);
    uint64_t mant = static_cast<uint64_t>(std::ldexp(fmant, 63));

    out[0] = static_cast<uint8_t>(((sign & 1) << 7) | ((ieee_exp >> 8) & 0x7F));
    out[1] = static_cast<uint8_t>(ieee_exp & 0xFF);
    for (int i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<uint8_t>((mant >> (56 - 8 * i)) & 0xFF);
    }
}

double ieee80_to_double(const uint8_t in[10]) {
    uint8_t sign = (in[0] >> 7) & 1;
    uint16_t exp = (static_cast<uint16_t>(in[0] & 0x7F) << 8) | in[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) {
        mant = (mant << 8) | in[2 + i];
    }

    if (exp == 0 && mant == 0) return 0.0;
    if (exp == 0x7FFF) {
        return mant == 0 ? (sign ? -HUGE_VAL : HUGE_VAL) : NAN;
    }

    double res = std::ldexp(static_cast<double>(mant), static_cast<int>(exp) - 16383 - 63);
    return sign ? -res : res;
}

uint32_t ieee80_to_uint32(const uint8_t in[10]) {
    double d = ieee80_to_double(in);
    if (d <= 0.0) return 0;
    if (d > 4294967295.0) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(std::round(d));
}

} // namespace audio_codecs::aiff
