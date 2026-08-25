#pragma once

#include "include/audio_codecs/aac/aac_types.h"
#include "src/core/fft.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class PsychoacousticModel {
public:
    PsychoacousticModel();

    void init(uint32_t sample_rate);
    void reset();

    // Psychoacoustic analysis for Long window (2048 samples -> 1024 spectral lines)
    // out_thresholds: masking threshold per SWB (size num_swb)
    // out_energy: spectral energy per SWB (size num_swb)
    // out_pe: Perceptual Entropy in bits
    void analyze_long(const float* pcm_2048, 
                      uint32_t sample_rate, 
                      float* out_thresholds, 
                      float* out_energy, 
                      float& out_pe);

    // Psychoacoustic analysis for Short window (256 samples -> 128 spectral lines)
    // out_thresholds: masking threshold per short SWB (size num_swb)
    // out_energy: spectral energy per short SWB (size num_swb)
    // out_pe: Perceptual Entropy in bits
    void analyze_short(const float* pcm_256, 
                       uint32_t sample_rate, 
                       float* out_thresholds, 
                       float* out_energy, 
                       float& out_pe);

private:
    uint32_t sample_rate_{44100};
    core::Fft2048 fft_long_;
    core::Fft256 fft_short_;
    float hanning_long_[AAC_WINDOW_LEN_LONG];
    float hanning_short_[AAC_WINDOW_LEN_SHORT];
    float ath_table_long_[AAC_MAX_SCALEFACTOR_BANDS];
    float ath_table_short_[AAC_MAX_SCALEFACTOR_BANDS];
    bool initialized_{false};

    void init_tables();
};

} // namespace audio_codecs::aac
