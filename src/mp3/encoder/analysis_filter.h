#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class AnalysisFilter {
public:
    AnalysisFilter();

    void reset();

    // Filter 32 time-domain PCM samples into 32 subband frequency samples
    void filter_pcm(const float* in_pcm_32, float* out_subband_32);

private:
    float x_buffer_[512];
    bool initialized_{false};
    float m_matrix_[32][64];

    void init_matrix();
};

} // namespace audio_codecs::mp3
