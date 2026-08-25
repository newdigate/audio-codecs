#pragma once
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/vorbis/vorbis_types.h"
#include "audio_codecs/ogg/ogg_demuxer.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::vorbis {

class VorbisDecoder : public AudioDecoder {
public:
    VorbisDecoder();
    ~VorbisDecoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Decode from raw byte stream (can be Ogg bitstream or raw Vorbis packets)
    // Returns total interleaved PCM samples written to out_pcm, or negative on error
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    // Process a single Vorbis header packet (0x01 ID, 0x03 Comment, 0x05 Setup)
    bool parse_header_packet(const uint8_t* in_packet, size_t in_bytes);

    // Decode a single Vorbis audio packet into out_pcm
    int decode_packet(const uint8_t* in_packet, size_t in_bytes, 
                      float* out_pcm, size_t max_out_samples);

    // Feed and decode Ogg container pages directly
    int decode_ogg_page(const uint8_t* page_data, size_t page_len, 
                        float* out_pcm, size_t max_out_samples);

    bool has_headers() const;
    const VorbisInfo& get_info() const;
    const VorbisComment& get_comment() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[1048576]; // 1 MB static buffer for zero-alloc state
    Impl* impl_{nullptr};
};

} // namespace audio_codecs::vorbis
