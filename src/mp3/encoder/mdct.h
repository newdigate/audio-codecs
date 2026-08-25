#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class ForwardMdct {
public:
    ForwardMdct();

    void reset();

    // Transform 36 subband time samples into 18 MDCT spectral values
    // block_type: 0 (normal), 1 (start), 2 (short), 3 (stop)
    void transform_subband(const float* time_36, float* mdct_18, int block_type = 0);

private:
    bool initialized_{false};
    float cos_36_[18][36];
    float cos_12_[6][12];
    float win_normal_[36];
    float win_start_[36];
    float win_stop_[36];
    float win_short_[12];

    void init_tables();
};

} // namespace audio_codecs::mp3
