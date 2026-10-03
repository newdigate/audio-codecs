#pragma once
#include "spectrum_types.h"
#include <cstdint>
#include <cstddef>

namespace audio_codecs::spectrum {

class SpectrumFilterbank {
public:
    SpectrumFilterbank();

    bool init(uint32_t sample_rate = 44100,
              uint8_t num_bands = DEFAULT_NUM_BANDS,
              uint16_t min_freq = 20,
              uint16_t max_freq = 20000);

    void apply_hann_window(const int16_t* in_pcm, int16_t* out_windowed_pcm) const;
    void apply_window(const int16_t* in_pcm, int16_t* out_windowed_pcm) const {
        apply_hann_window(in_pcm, out_windowed_pcm);
    }
    void compute_bands(const float* in_magnitudes_512, uint8_t* out_bands) const;

    uint8_t num_bands() const { return num_bands_; }
    bool is_initialized() const { return initialized_; }
    uint16_t band_start_bin(uint8_t band) const { return (band < num_bands_) ? band_bin_start_[band] : 0; }
    uint16_t band_end_bin(uint8_t band) const { return (band < num_bands_) ? band_bin_end_[band] : 0; }

private:
    uint8_t num_bands_{DEFAULT_NUM_BANDS};
    uint16_t band_bin_start_[64]{};
    uint16_t band_bin_end_[64]{};
    int16_t hann_window_[1024]{};
    bool initialized_{false};
};

} // namespace audio_codecs::spectrum
