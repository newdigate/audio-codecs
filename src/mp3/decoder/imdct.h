#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class ImdctEngine {
public:
    ImdctEngine();

    void reset();

    // Transform one subband (18 spectral values) into 18 time-domain output samples
    // ch: 0 or 1, sb: 0..31, block_type: 0 (normal), 1 (start), 2 (short), 3 (stop)
    void transform_subband(const float* xr_18, int ch, int sb, float* out_18, int block_type = 0);

private:
    float overlap_[2][32][18]; // [ch][sb][18] history for overlap-add
    bool initialized_{false};
    float cos_36_[36][18];
    float cos_12_[12][6];
    float win_normal_[36];
    float win_start_[36];
    float win_stop_[36];
    float win_short_[12];

    void init_tables();
};

} // namespace audio_codecs::mp3
