#pragma once

#include <cstdint>
#include <cstddef>

namespace audio_codecs::tempo {

struct NoveltySample {
    float bass_flux;
    float snare_flux;
    float high_flux;
    float raw_novelty;
    float novelty; // thresholded
};

class TempoNoveltyExtractor {
public:
    TempoNoveltyExtractor();
    void reset();

    NoveltySample process_frame(const uint8_t bands[64]);

private:
    uint8_t prev_bands_[64];
    bool has_prev_frame_;

    static constexpr size_t K_THRESHOLD_WINDOW = 9;
    float threshold_history_[K_THRESHOLD_WINDOW];
    size_t threshold_idx_;
    size_t threshold_count_;
};

} // namespace audio_codecs::tempo
