#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/encoder_interface.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

struct FlacEncoderConfig {
    AudioConfig core_config{44100, 2, 0, false, 2};
    uint8_t  compression_level{5}; // 0 (fastest/fixed) to 8 (max LPC search)
    uint16_t block_size{4096};     // Block size in samples (e.g. 1152, 4096)
    uint8_t  bit_depth{16};        // 16 or 24 bit PCM
};

template <size_t MaxChannels = 2, size_t MaxBlockSize = 4096>
class FlacEncoderBase : public AudioEncoder {
public:
    FlacEncoderBase();
    ~FlacEncoderBase() override;

    bool init(const AudioConfig& config) override;
    bool init_flac(const FlacEncoderConfig& config);
    void reset() override;

    // Polymorphic normalized float [-1.0, 1.0] encoder
    int encode_frame(const float* in_pcm, size_t in_samples, 
                     uint8_t* out_data, size_t max_out_bytes) override;

    // Bit-exact integer encoders
    int encode_frame_i32(const int32_t* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);
    int encode_frame_i16(const int16_t* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);

    // Write "fLaC" container + STREAMINFO header (42 bytes)
    int write_stream_header(uint8_t* out_data, size_t max_out_bytes);

    // Finalize stream (computes unencoded PCM MD5 and updates STREAMINFO)
    int finish_stream(uint8_t* streaminfo_header_ptr);
    int flush(uint8_t* out_data, size_t max_out_bytes) override;

    uint32_t get_sample_rate() const;
    uint8_t  get_channels() const;
    uint8_t  get_bit_depth() const;
    uint16_t get_block_size() const;
    uint64_t get_total_samples() const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[sizeof(int32_t) * (MaxChannels * 2 + 2) * MaxBlockSize + 1024];
    Impl* impl_{nullptr};
};

using FlacEncoder = FlacEncoderBase<2, 4096>;

} // namespace audio_codecs::flac

#include "src/flac/encoder/flac_encoder_impl.h"
