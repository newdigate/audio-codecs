#include <audio_codecs/tempo/tempo_inducer.h>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::tempo {

TempoInducer::TempoInducer() {
    reset();
}

void TempoInducer::reset(float frame_rate) {
    std::memset(buffer_, 0, sizeof(buffer_));
    std::memset(temp_unrolled_, 0, sizeof(temp_unrolled_));
    head_ = 0;
    count_ = 0;
    frame_rate_ = frame_rate > 0.0f ? frame_rate : 86.133f;
}

void TempoInducer::feed_sample(float novelty) {
    buffer_[head_] = novelty;
    head_ = (head_ + 1) % K_WINDOW_SIZE;
    if (count_ < K_WINDOW_SIZE) {
        count_++;
    }
}

float TempoInducer::prior_weight(float lag) const {
    if (lag <= 0.0f) return 0.0f;
    float bpm = (60.0f * frame_rate_) / lag;
    // Log-Gaussian prior centered at 120 BPM with sigma = 0.8 octaves
    float log_diff = std::log2(bpm / 120.0f);
    return std::exp(- (log_diff * log_diff) / (2.0f * 0.8f * 0.8f));
}

TempoEstimate TempoInducer::estimate_tempo() const {
    if (count_ < K_MAX_LAG * 2) {
        return {120.0f, 0, 43.0f};
    }

    // Unroll circular buffer into contiguous temporal array
    for (size_t i = 0; i < K_WINDOW_SIZE; ++i) {
        size_t idx = (head_ + i) % K_WINDOW_SIZE;
        temp_unrolled_[i] = buffer_[idx];
    }

    float comb_scores[K_MAX_LAG + 1];
    std::memset(comb_scores, 0, sizeof(comb_scores));

    float max_score = -1.0f;
    int best_lag = K_MIN_LAG;
    float sum_scores = 0.0f;
    int valid_lags = 0;

    for (int tau = K_MIN_LAG; tau <= K_MAX_LAG; ++tau) {
        // Autocorrelation at tau
        float r1 = 0.0f;
        for (int k = 0; k < static_cast<int>(K_WINDOW_SIZE) - tau; ++k) {
            r1 += temp_unrolled_[k] * temp_unrolled_[k + tau];
        }

        // Comb harmonic at 2*tau
        float r2 = 0.0f;
        if (2 * tau < static_cast<int>(K_WINDOW_SIZE)) {
            for (int k = 0; k < static_cast<int>(K_WINDOW_SIZE) - 2 * tau; ++k) {
                r2 += temp_unrolled_[k] * temp_unrolled_[k + 2 * tau];
            }
        }

        // Comb harmonic at 4*tau
        float r4 = 0.0f;
        if (4 * tau < static_cast<int>(K_WINDOW_SIZE)) {
            for (int k = 0; k < static_cast<int>(K_WINDOW_SIZE) - 4 * tau; ++k) {
                r4 += temp_unrolled_[k] * temp_unrolled_[k + 4 * tau];
            }
        }

        float score = (r1 + 0.5f * r2 + 0.25f * r4) * prior_weight(static_cast<float>(tau));
        comb_scores[tau] = score;
        sum_scores += score;
        valid_lags++;

        if (score > max_score) {
            max_score = score;
            best_lag = tau;
        }
    }

    // 3-point parabolic interpolation around peak
    float sub_lag = static_cast<float>(best_lag);
    if (best_lag > K_MIN_LAG && best_lag < K_MAX_LAG) {
        float y_prev = comb_scores[best_lag - 1];
        float y_cur  = comb_scores[best_lag];
        float y_next = comb_scores[best_lag + 1];
        float denom = 2.0f * (y_prev - 2.0f * y_cur + y_next);
        if (std::fabs(denom) > 1e-6f) {
            float delta = (y_prev - y_next) / denom;
            if (delta >= -1.0f && delta <= 1.0f) {
                sub_lag += delta;
            }
        }
    }

    float mean_score = valid_lags > 0 ? (sum_scores / valid_lags) : 0.0f;
    float confidence_ratio = max_score > mean_score ? ((max_score - mean_score) / (max_score + 1e-4f)) : 0.0f;
    uint8_t confidence = static_cast<uint8_t>(std::clamp(confidence_ratio * 255.0f, 0.0f, 255.0f));

    float bpm = (60.0f * frame_rate_) / sub_lag;
    return {bpm, confidence, sub_lag};
}

} // namespace audio_codecs::tempo
