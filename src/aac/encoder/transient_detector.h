#pragma once

#include "include/audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class TransientDetector {
public:
    TransientDetector();

    void reset();

    // Updates detector with 2048 PCM samples and returns the decided WindowSequence
    WindowSequence update(const float* pcm_2048, bool force_long = false);

    // Get current window sequence state
    WindowSequence get_current_sequence() const { return current_seq_; }

    // Evaluates energy jump ratio across 8 sub-blocks (256 samples each)
    bool detect_attack(const float* pcm_2048) const;

private:
    WindowSequence current_seq_{WindowSequence::OnlyLong};
    float prev_subblock_energy_{0.0f};
};

} // namespace audio_codecs::aac
