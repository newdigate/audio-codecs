#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/wav/wav_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::wav {

template <size_t MaxChannels = 2>
class WavDecoderBase : public AudioDecoder {
public:
    WavDecoderBase();
    ~WavDecoderBase() override = default;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Polymorphic normalized float [-1.0, 1.0] decoder
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    // Direct integer and float decoders
    int decode_frame_i16(const uint8_t* in_data, size_t in_bytes, 
                         int16_t* out_pcm, size_t max_out_samples);
    int decode_frame_i32(const uint8_t* in_data, size_t in_bytes, 
                         int32_t* out_pcm, size_t max_out_samples);
    int decode_frame_f32(const uint8_t* in_data, size_t in_bytes, 
                         float* out_pcm, size_t max_out_samples);

    // Stream container parser (detects RIFF, WAVE, fmt, fact, data chunks)
    bool parse_stream_header(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed);

    // Stream metadata accessors
    uint32_t get_sample_rate() const;
    uint8_t  get_channels() const;
    uint8_t  get_bit_depth() const;
    WavFormat get_format_tag() const;
    WavSampleFormat get_sample_format() const;
    uint32_t get_channel_mask() const;
    uint64_t get_total_samples() const;
    size_t   get_last_frame_bytes() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[256];
    Impl* impl_{nullptr};
};

using WavDecoder = WavDecoderBase<2>;

} // namespace audio_codecs::wav

#include "src/wav/decoder/wav_decoder_impl.h"
