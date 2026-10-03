#pragma once

#include <cstdint>
#include <cstddef>

namespace audio_codecs::tempo {

struct TempoEstimate {
    float bpm;
    uint8_t confidence;
    float lag_frames;
};

class TempoInducer {
public:
    static constexpr size_t K_WINDOW_SIZE = 512;
    static constexpr int K_MIN_LAG = 21; // ~240 BPM at 86.13 Hz
    static constexpr int K_MAX_LAG = 89; // ~58 BPM at 86.13 Hz

    TempoInducer();
    void reset(float frame_rate = 86.133f);

    void feed_sample(float novelty);
    TempoEstimate estimate_tempo() const;

private:
    float buffer_[K_WINDOW_SIZE];
    mutable float temp_unrolled_[K_WINDOW_SIZE];
    size_t head_;
    size_t count_;
    float frame_rate_;

    float prior_weight(float lag) const;
};

} // namespace audio_codecs::tempo
