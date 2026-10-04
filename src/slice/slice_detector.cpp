#include <audio_codecs/slice/slice_detector.h>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace audio_codecs::slice {

SliceDetector::SliceDetector() {
    reset();
}

bool SliceDetector::init(const DetectorConfig& config) {
    config_ = config;
    reset();
    return true;
}

void SliceDetector::reset() {
    prev_raw_sample_ = 0;
    prev_hop_energy_ = 0.0f;
    total_samples_processed_ = 0;
    last_transient_sample_ = 0;
    has_previous_transient_ = false;
    history_idx_ = 0;
    history_count_ = 0;
    std::memset(energy_history_, 0, sizeof(energy_history_));
}

uint32_t SliceDetector::find_zero_crossing_backward(const int16_t* pcm_samples,
                                                    uint32_t peak_index,
                                                    uint32_t max_search_samples) {
    if (pcm_samples == nullptr || peak_index == 0) {
        return peak_index;
    }

    uint32_t limit = (peak_index > max_search_samples) ? (peak_index - max_search_samples) : 0;
    for (uint32_t i = peak_index; i > limit; --i) {
        int32_t s0 = pcm_samples[i - 1];
        int32_t s1 = pcm_samples[i];
        if ((s0 <= 0 && s1 >= 0) || (s0 >= 0 && s1 <= 0)) {
            return i;
        }
    }
    return peak_index;
}

uint32_t SliceDetector::process_block(const int16_t* pcm_samples, uint32_t count,
                                      uint32_t* out_transient_indices, uint32_t max_indices) {
    if (pcm_samples == nullptr || count == 0 || out_transient_indices == nullptr || max_indices == 0) {
        return 0;
    }

    uint32_t detected_count = 0;
    uint32_t num_hops = count / HOP_SIZE;

    // Sensitivity factor mapping: sensitivity 0.0 -> k = 3.5, sensitivity 1.0 -> k = 0.5
    float k_sens = 3.5f - (config_.sensitivity * 3.0f);

    for (uint32_t h = 0; h < num_hops; ++h) {
        uint32_t hop_start = h * HOP_SIZE;
        float hop_energy = 0.0f;

        // 1st-order pre-emphasis and micro-hop energy accumulation
        for (uint32_t i = 0; i < HOP_SIZE; ++i) {
            int16_t raw = pcm_samples[hop_start + i];
            float hp = static_cast<float>(raw) - config_.pre_emphasis_alpha * static_cast<float>(prev_raw_sample_);
            prev_raw_sample_ = raw;
            hop_energy += std::abs(hp);
        }

        // Half-wave rectified novelty
        float novelty = (hop_energy > prev_hop_energy_) ? (hop_energy - prev_hop_energy_) : 0.0f;
        prev_hop_energy_ = hop_energy;

        // Compute rolling mean and variance
        float sum = 0.0f;
        uint32_t n = (history_count_ < ROLLING_HOPS) ? history_count_ : ROLLING_HOPS;
        for (uint32_t i = 0; i < n; ++i) {
            sum += energy_history_[i];
        }
        float mean = (n > 0) ? (sum / static_cast<float>(n)) : 0.0f;

        float var_sum = 0.0f;
        for (uint32_t i = 0; i < n; ++i) {
            float diff = energy_history_[i] - mean;
            var_sum += diff * diff;
        }
        float stddev = (n > 0) ? std::sqrt(var_sum / static_cast<float>(n)) : 0.0f;

        // Push into circular history
        energy_history_[history_idx_] = novelty;
        history_idx_ = (history_idx_ + 1) % ROLLING_HOPS;
        if (history_count_ < ROLLING_HOPS) {
            history_count_++;
        }

        float threshold = mean + k_sens * stddev + 500.0f; // 500 noise floor

        if (novelty > threshold && novelty > 2000.0f) {
            uint32_t global_hop_sample = total_samples_processed_ + hop_start;
            if (!has_previous_transient_ || (global_hop_sample - last_transient_sample_ >= config_.min_slice_samples)) {
                // Find local peak sample in this hop
                uint32_t hop_end = std::min(hop_start + HOP_SIZE, count);
                uint32_t search_peak = hop_start;
                int32_t max_val = -1;
                for (uint32_t i = hop_start; i < hop_end; ++i) {
                    int32_t a = std::abs(static_cast<int32_t>(pcm_samples[i]));
                    if (a > max_val) {
                        max_val = a;
                        search_peak = i;
                    }
                }

                uint32_t local_zc = find_zero_crossing_backward(pcm_samples, search_peak, 64);
                uint32_t snapped_sample = total_samples_processed_ + local_zc;

                if (detected_count < max_indices) {
                    out_transient_indices[detected_count++] = snapped_sample;
                }
                last_transient_sample_ = snapped_sample;
                has_previous_transient_ = true;
            }
        }
    }

    total_samples_processed_ += count;
    return detected_count;
}

} // namespace audio_codecs::slice
