#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/encoder_interface.h"
#include "audio_codecs/wav/wav_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::wav {

template <size_t MaxChannels = 2>
class WavEncoderBase : public AudioEncoder {
public:
    WavEncoderBase();
    ~WavEncoderBase() override = default;

    bool init(const AudioConfig& config) override;
    bool init_wav(const WavEncoderConfig& config);
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

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[256];
    Impl* impl_{nullptr};
};

using WavEncoder = WavEncoderBase<2>;

} // namespace audio_codecs::wav

#include "src/wav/encoder/wav_encoder_impl.h"
