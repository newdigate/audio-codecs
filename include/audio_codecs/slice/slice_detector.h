#pragma once

#include <audio_codecs/slice/slice_types.h>
#include <cstdint>
#include <cstddef>

namespace audio_codecs::slice {

struct DetectorConfig {
    float sensitivity = 0.5f;          // 0.0f (least sensitive) to 1.0f (most sensitive)
    uint32_t min_slice_samples = 512;    // Minimum samples between slices (~11.6 ms @ 44.1k)
    float pre_emphasis_alpha = 0.95f;  // High-pass filter coefficient
    uint32_t sample_rate = 44100;
};

class SliceDetector {
public:
    static constexpr uint32_t HOP_SIZE = 32;       // Micro-hop size in samples
    static constexpr uint32_t ROLLING_HOPS = 16;   // History window for adaptive threshold

    SliceDetector();
    bool init(const DetectorConfig& config);
    void reset();

    // Process a block of mono PCM samples
    // Returns number of transients detected in this block
    uint32_t process_block(const int16_t* pcm_samples, uint32_t count,
                           uint32_t* out_transient_indices, uint32_t max_indices);

    // Search backward from peak_index to find the zero-crossing sample
    static uint32_t find_zero_crossing_backward(const int16_t* pcm_samples,
                                                uint32_t peak_index,
                                                uint32_t max_search_samples);

private:
    DetectorConfig config_{};
    int16_t prev_raw_sample_ = 0;
    float prev_hop_energy_ = 0.0f;
    uint32_t total_samples_processed_ = 0;
    uint32_t last_transient_sample_ = 0;
    bool has_previous_transient_ = false;

    // Rolling statistics for adaptive threshold
    float energy_history_[ROLLING_HOPS]{};
    uint32_t history_idx_ = 0;
    uint32_t history_count_ = 0;
};

} // namespace audio_codecs::slice
