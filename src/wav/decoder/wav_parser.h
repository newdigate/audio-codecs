#pragma once
#include "audio_codecs/wav/wav_types.h"
#include "src/wav/wav_common.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::wav {

enum class ParserState {
    SearchRiff,
    ReadChunkHeader,
    ReadFmtPayload,
    ReadFactPayload,
    SkipChunkPayload,
    DataReady,
    Error
};

class WavParser {
public:
    WavParser();
    void reset();

    // Stream-oriented chunk scanner. Returns true if stream is valid, false on corruption.
    bool parse_chunk_stream(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed);

    bool is_header_complete() const { return state_ == ParserState::DataReady; }

    uint32_t sample_rate() const { return sample_rate_; }
    uint8_t  channels() const { return channels_; }
    uint8_t  bits_per_sample() const { return bits_per_sample_; }
    uint16_t block_align() const { return block_align_; }
    WavFormat format_tag() const { return format_tag_; }
    WavSampleFormat sample_format() const { return sample_format_; }
    uint32_t channel_mask() const { return channel_mask_; }
    uint64_t total_samples() const { return total_samples_; }
    uint32_t data_chunk_size() const { return data_chunk_size_; }

private:
    ParserState state_{ParserState::SearchRiff};
    uint8_t buffer_[64];
    size_t buf_pos_{0};

    uint32_t current_chunk_id_{0};
    uint32_t current_chunk_size_{0};
    uint32_t bytes_to_skip_{0};

    uint32_t sample_rate_{0};
    uint8_t  channels_{0};
    uint8_t  bits_per_sample_{0};
    uint16_t block_align_{0};
    WavFormat format_tag_{WavFormat::Pcm};
    WavSampleFormat sample_format_{WavSampleFormat::Int16LE};
    uint32_t channel_mask_{0};
    uint64_t total_samples_{0};
    uint32_t data_chunk_size_{0};
    bool fmt_parsed_{false};

    bool process_fmt_chunk();
};

} // namespace audio_codecs::wav
