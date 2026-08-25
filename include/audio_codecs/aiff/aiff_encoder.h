#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/encoder_interface.h"
#include "audio_codecs/aiff/aiff_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aiff {

template <size_t MaxChannels = 2>
class AiffEncoderBase : public AudioEncoder {
public:
    AiffEncoderBase();
    ~AiffEncoderBase() override = default;

    bool init(const AudioConfig& config) override;
    bool init_aiff(const AiffEncoderConfig& config);
    void reset() override;

    // Polymorphic normalized float [-1.0, 1.0] encoder
    int encode_frame(const float* in_pcm, size_t in_samples, 
                     uint8_t* out_data, size_t max_out_bytes) override;

    // Direct integer and float encoders
    int encode_frame_i16(const int16_t* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);
    int encode_frame_i32(const int32_t* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);
    int encode_frame_f32(const float* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);

    // Header generation & in-place finalization
    int write_stream_header(uint8_t* out_data, size_t max_out_bytes);
    int finalize_header(uint8_t* header_ptr, uint32_t total_data_bytes);
    int flush(uint8_t* out_data, size_t max_out_bytes) override;

    // Stream metadata accessors
    uint32_t get_sample_rate() const;
    uint8_t  get_channels() const;
    uint8_t  get_bit_depth() const;
    uint64_t get_total_samples() const;
    AiffFormType get_form_type() const;
    AiffCompressionType get_compression_type() const;
    AiffSampleFormat get_sample_format() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[256];
    Impl* impl_{nullptr};
};

using AiffEncoder = AiffEncoderBase<2>;

} // namespace audio_codecs::aiff

#include "src/aiff/encoder/aiff_encoder_impl.h"
