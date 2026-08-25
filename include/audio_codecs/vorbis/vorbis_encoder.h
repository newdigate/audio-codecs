#pragma once
#include "audio_codecs/core/encoder_interface.h"
#include "audio_codecs/vorbis/vorbis_types.h"
#include "audio_codecs/ogg/ogg_muxer.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::vorbis {

class VorbisEncoder : public AudioEncoder {
public:
    VorbisEncoder();
    ~VorbisEncoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Encode interleaved PCM into Ogg/Vorbis bitstream
    // Returns total bytes written to out_data, or negative on error
    int encode_frame(const float* in_pcm, size_t in_samples,
                     uint8_t* out_data, size_t max_out_bytes) override;

    // Flush any pending packets/pages
    int flush(uint8_t* out_data, size_t max_out_bytes) override;

    // Explicitly write Ogg headers (ID, Comment, Setup) to out_data
    size_t write_headers(uint8_t* out_data, size_t max_out_bytes);

    // Encode a single raw Vorbis audio packet (without Ogg container framing)
    int encode_packet(const float* const* in_channel_pcm, size_t blocksize,
                      uint8_t* out_packet, size_t max_out_bytes);

    const VorbisInfo& get_info() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[1048576]; // 1 MB static buffer for zero-alloc state
    Impl* impl_{nullptr};
};

} // namespace audio_codecs::vorbis
