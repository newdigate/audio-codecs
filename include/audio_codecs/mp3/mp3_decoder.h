#pragma once
#include "audio_codecs/core/decoder_interface.h"

namespace audio_codecs::mp3 {

class Mp3Decoder : public AudioDecoder {
public:
    Mp3Decoder();
    ~Mp3Decoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Decode a single MP3 frame from in_data (finds syncword automatically)
    // Returns total interleaved PCM samples written to out_pcm, or negative error code
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    // Helper: inspect last parsed frame header info
    bool get_frame_info(uint32_t& sample_rate, uint8_t& channels, uint32_t& bitrate_kbps) const;
    size_t get_last_frame_bytes() const;
    size_t get_last_sync_offset() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[131072]; // Statically pre-allocated state memory
    Impl* impl_{nullptr};
};

} // namespace audio_codecs::mp3
