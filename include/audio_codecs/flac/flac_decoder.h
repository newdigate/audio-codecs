#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

template <size_t MaxChannels = 2, size_t MaxBlockSize = 4096>
class FlacDecoderBase : public AudioDecoder {
public:
    FlacDecoderBase();
    ~FlacDecoderBase() override;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Polymorphic normalized float [-1.0, 1.0] decoder
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    // Bit-exact integer decoders (zero float conversion overhead)
    int decode_frame_i32(const uint8_t* in_data, size_t in_bytes, 
                         int32_t* out_pcm, size_t max_out_samples);
    int decode_frame_i16(const uint8_t* in_data, size_t in_bytes, 
                         int16_t* out_pcm, size_t max_out_samples);

    // Stream container parser (detects "fLaC", STREAMINFO, metadata blocks)
    bool parse_stream_header(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed);

    // Stream metadata accessors
    uint32_t get_sample_rate() const;
    uint8_t  get_channels() const;
    uint8_t  get_bit_depth() const;
    uint64_t get_total_samples() const;
    size_t   get_last_frame_bytes() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[sizeof(int32_t) * (MaxChannels + 1) * MaxBlockSize + 512];
    Impl* impl_{nullptr};
};

using FlacDecoder = FlacDecoderBase<2, 4096>;

} // namespace audio_codecs::flac

#include "src/flac/decoder/flac_decoder_impl.h"
