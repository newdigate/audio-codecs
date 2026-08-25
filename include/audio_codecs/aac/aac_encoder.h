#pragma once

#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/encoder_interface.h"
#include "audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class AacEncoder : public AudioEncoder {
public:
    AacEncoder();
    ~AacEncoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;
    int encode_frame(const float* in_pcm, size_t in_samples,
                     uint8_t* out_data, size_t max_out_bytes) override;
    int flush(uint8_t* out_data, size_t max_out_bytes) override;

    const AudioConfig& get_config() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[131072];
    Impl* impl_{nullptr};
};

} // namespace audio_codecs::aac
