#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::core {

class Fft1024 {
public:
    static constexpr size_t N = 1024;
    static constexpr size_t NUM_BINS = N / 2 + 1; // 513

    Fft1024();

    void init();

    // In-place real FFT: takes 1024 real samples, outputs 513 complex bins (real & imag)
    void transform_real(const float* time_in, float* real_out, float* imag_out);

private:
    bool initialized_{false};
    float cos_table_[N / 2];
    float sin_table_[N / 2];
    uint16_t bit_rev_[N];
};

} // namespace audio_codecs::core
