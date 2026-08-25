#pragma once
#include "src/core/fft.h"
#include "src/mp3/mp3_tables.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class PsychoacousticModel {
public:
    PsychoacousticModel();

    void init(uint32_t sample_rate);
    void reset();

    // Calculate perceptual masking thresholds and SMR for 22 scalefactor bands
    void calculate_masking(const float* in_pcm_1024, float* mask_thresholds_22, float* smr_22);

private:
    uint32_t sample_rate_{44100};
    core::Fft1024 fft_;
    float hanning_window_[1024];
    float ath_table_[22];
    bool initialized_{false};

    void init_tables();
};

} // namespace audio_codecs::mp3
