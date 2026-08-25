#pragma once

#include "include/audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class AacMdct {
public:
    AacMdct();

    // Forward MDCT transforms (time -> freq)
    // Applies window according to shape/sequence, then transforms to frequency domain.
    void forward_long(const float* in_time_2048, float* out_freq_1024, WindowShape shape = WindowShape::Sine);
    void forward_short(const float* in_time_256, float* out_freq_128, WindowShape shape = WindowShape::Sine);
    void forward_eight_short(const float* in_time_2048, float* out_freq_1024, WindowShape shape = WindowShape::Sine);

    // Inverse MDCT transforms (freq -> time)
    // Transforms to time domain, then applies synthesis window according to shape/sequence.
    void inverse_long(const float* in_freq_1024, float* out_time_2048, WindowShape shape = WindowShape::Sine);
    void inverse_short(const float* in_freq_128, float* out_time_256, WindowShape shape = WindowShape::Sine);
    void inverse_eight_short(const float* in_freq_1024, float* out_time_2048, WindowShape shape = WindowShape::Sine);

    // Arbitrary window transitions (OnlyLong, LongStart, EightShort, LongStop)
    void forward_windowed(const float* in_time_2048, float* out_freq_1024,
                          WindowSequence seq, WindowShape shape_prev, WindowShape shape_curr);
    void inverse_windowed(const float* in_freq_1024, float* out_time_2048,
                          WindowSequence seq, WindowShape shape_prev, WindowShape shape_curr);

    // Raw (unwindowed) core MDCT / IMDCT transforms
    void mdct_core_long(const float* in_time_2048, float* out_freq_1024);
    void imdct_core_long(const float* in_freq_1024, float* out_time_2048);
    void mdct_core_short(const float* in_time_256, float* out_freq_128);
    void imdct_core_short(const float* in_freq_128, float* out_time_256);

private:
    struct Complex {
        float re;
        float im;
    };

    void init_tables();

    // 512-point FFT tables (for Long transforms: N=1024, M=512)
    Complex pre_twiddle_long_[512];
    Complex post_twiddle_long_[512];
    Complex fft_twiddle_512_[256];
    uint16_t bit_rev_512_[512];

    // 64-point FFT tables (for Short transforms: N=128, M=64)
    Complex pre_twiddle_short_[64];
    Complex post_twiddle_short_[64];
    Complex fft_twiddle_64_[32];
    uint16_t bit_rev_64_[64];

    void fft_512(Complex* data) const;
    void fft_64(Complex* data) const;
};

} // namespace audio_codecs::aac
