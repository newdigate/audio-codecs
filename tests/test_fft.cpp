// tests/test_fft.cpp
#include "src/core/fft.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::core;
    Fft1024 fft;
    fft.init();

    float time_in[1024] = {0.0f};
    float real_out[513] = {0.0f};
    float imag_out[513] = {0.0f};

    // 16 cycles in 1024 samples (bin 16)
    for (int i = 0; i < 1024; ++i) {
        time_in[i] = std::cos(audio_codecs::constants::TWO_PI * 16.0f * i / 1024.0f);
    }

    fft.transform_real(time_in, real_out, imag_out);

    float power16 = real_out[16]*real_out[16] + imag_out[16]*imag_out[16];
    float power0 = real_out[0]*real_out[0] + imag_out[0]*imag_out[0];
    float power15 = real_out[15]*real_out[15] + imag_out[15]*imag_out[15];

    assert(power16 > 1000.0f);
    assert(power0 < 0.1f);
    assert(power15 < 0.1f);

    std::cout << "FFT tests passed!\n";
    return 0;
}
