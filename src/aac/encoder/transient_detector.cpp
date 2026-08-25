#include "src/aac/encoder/transient_detector.h"
#include <algorithm>
#include <cmath>

namespace audio_codecs::aac {

TransientDetector::TransientDetector() {
    reset();
}

void TransientDetector::reset() {
    current_seq_ = WindowSequence::OnlyLong;
    prev_subblock_energy_ = 0.0f;
}

bool TransientDetector::detect_attack(const float* pcm_2048) const {
    if (!pcm_2048) return false;

    float energies[8] = {0.0f};

    // Calculate high-pass filtered energy for 8 sub-blocks of 256 samples
    for (int b = 0; b < 8; ++b) {
        float sum = 0.0f;
        float prev_s = 0.0f;
        for (int i = 0; i < 256; ++i) {
            float s = pcm_2048[b * 256 + i];
            float diff = s - 0.95f * prev_s;
            sum += diff * diff;
            prev_s = s;
        }
        energies[b] = sum;
    }

    // 1. Check jump from previous frame if previous energy was established
    if (prev_subblock_energy_ > 1e-4f) {
        if (energies[0] > 8.0f * prev_subblock_energy_ && energies[0] > 1e-3f) {
            return true;
        }
    }

    // 2. Check energy jumps across sub-blocks within current frame
    float prev_accum = std::max(energies[0], 1e-5f);
    for (int b = 1; b < 8; ++b) {
        float e = energies[b];
        float prev_mean = prev_accum / static_cast<float>(b);
        float ref_energy = std::max(energies[b - 1], prev_mean);

        if (e > 1e-3f && e > 8.0f * std::max(ref_energy, 1e-4f)) {
            return true;
        }
        prev_accum += e;
    }

    return false;
}

WindowSequence TransientDetector::update(const float* pcm_2048, bool force_long) {
    bool is_transient = false;
    if (pcm_2048 && !force_long) {
        is_transient = detect_attack(pcm_2048);

        // Update previous energy estimate with last sub-block energy
        float sum = 0.0f;
        float prev_s = 0.0f;
        for (int i = 0; i < 256; ++i) {
            float s = pcm_2048[7 * 256 + i];
            float diff = s - 0.95f * prev_s;
            sum += diff * diff;
            prev_s = s;
        }
        prev_subblock_energy_ = sum;
    }

    switch (current_seq_) {
        case WindowSequence::OnlyLong:
            if (force_long) {
                current_seq_ = WindowSequence::OnlyLong;
            } else {
                current_seq_ = is_transient ? WindowSequence::LongStart : WindowSequence::OnlyLong;
            }
            break;

        case WindowSequence::LongStart:
            current_seq_ = WindowSequence::EightShort;
            break;

        case WindowSequence::EightShort:
            if (force_long || !is_transient) {
                current_seq_ = WindowSequence::LongStop;
            } else {
                current_seq_ = WindowSequence::EightShort;
            }
            break;

        case WindowSequence::LongStop:
            if (force_long) {
                current_seq_ = WindowSequence::OnlyLong;
            } else {
                current_seq_ = is_transient ? WindowSequence::LongStart : WindowSequence::OnlyLong;
            }
            break;
    }

    return current_seq_;
}

} // namespace audio_codecs::aac
