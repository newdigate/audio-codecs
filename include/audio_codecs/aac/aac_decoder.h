#pragma once

#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class AacDecoder : public AudioDecoder {
public:
    AacDecoder();
    ~AacDecoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    bool get_frame_info(uint32_t& sample_rate, uint8_t& channels, uint32_t& bitrate_kbps) const;
    size_t get_last_frame_bytes() const;
    size_t get_last_sync_offset() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[131072]; // Pre-allocated state memory for zero dynamic allocations
    Impl* impl_{nullptr};
};

} // namespace audio_codecs::aac
