#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class SynthesisFilter {
public:
    SynthesisFilter();

    void reset();

    // Filter 32 subband samples for a single time step into 32 output PCM samples
    void filter_subband(const float* subband_32, float* out_pcm_32);

private:
    float v_buffer_[1024];
    bool initialized_{false};
    float n_matrix_[64][32];

    void init_matrix();
};

} // namespace audio_codecs::mp3
