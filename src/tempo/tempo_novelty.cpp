#include <audio_codecs/tempo/tempo_novelty.h>
#include <algorithm>
#include <cstring>

namespace audio_codecs::tempo {

TempoNoveltyExtractor::TempoNoveltyExtractor() {
    reset();
}

void TempoNoveltyExtractor::reset() {
    std::memset(prev_bands_, 0, sizeof(prev_bands_));
    has_prev_frame_ = false;
    std::memset(threshold_history_, 0, sizeof(threshold_history_));
    threshold_idx_ = 0;
    threshold_count_ = 0;
}

NoveltySample TempoNoveltyExtractor::process_frame(const uint8_t bands[64]) {
    NoveltySample sample{0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    if (!has_prev_frame_) {
        std::memcpy(prev_bands_, bands, 64);
        has_prev_frame_ = true;
    } else {
        // Sub-band half-wave rectified differences
        for (int b = 0; b <= 15; ++b) {
            if (bands[b] > prev_bands_[b]) {
                sample.bass_flux += static_cast<float>(bands[b] - prev_bands_[b]);
            }
        }
        for (int b = 16; b <= 42; ++b) {
            if (bands[b] > prev_bands_[b]) {
                sample.snare_flux += static_cast<float>(bands[b] - prev_bands_[b]);
            }
        }
        for (int b = 43; b <= 63; ++b) {
            if (bands[b] > prev_bands_[b]) {
                sample.high_flux += static_cast<float>(bands[b] - prev_bands_[b]);
            }
        }

        std::memcpy(prev_bands_, bands, 64);
    }

    sample.raw_novelty = 1.0f * sample.bass_flux + 0.8f * sample.snare_flux + 0.2f * sample.high_flux;

    // Rolling local threshold subtraction
    threshold_history_[threshold_idx_] = sample.raw_novelty;
    threshold_idx_ = (threshold_idx_ + 1) % K_THRESHOLD_WINDOW;
    if (threshold_count_ < K_THRESHOLD_WINDOW) {
        threshold_count_++;
    }

    float mean_val = 0.0f;
    for (size_t i = 0; i < threshold_count_; ++i) {
        mean_val += threshold_history_[i];
    }
    mean_val /= static_cast<float>(threshold_count_);

    sample.novelty = std::max(0.0f, sample.raw_novelty - mean_val);
    return sample;
}

} // namespace audio_codecs::tempo
