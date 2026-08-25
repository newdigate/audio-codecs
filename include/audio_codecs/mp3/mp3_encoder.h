#pragma once
#include "audio_codecs/core/encoder_interface.h"

namespace audio_codecs::mp3 {

class Mp3Encoder : public AudioEncoder {
public:
    Mp3Encoder();
    ~Mp3Encoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Encode one MP3 frame (1152 samples per channel for MPEG-1) from in_pcm
    // in_pcm: interleaved audio samples
    // in_samples: total interleaved sample count (e.g. 1152 * channels)
    // Returns total bytes written to out_data, or negative error code
    int encode_frame(const float* in_pcm, size_t in_samples,
                     uint8_t* out_data, size_t max_out_bytes) override;

    int flush(uint8_t* out_data, size_t max_out_bytes) override;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[131072]; // Pre-allocated state memory
    Impl* impl_{nullptr};
};

} // namespace audio_codecs::mp3
