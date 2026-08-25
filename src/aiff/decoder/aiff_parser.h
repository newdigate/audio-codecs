#pragma once
#include "audio_codecs/aiff/aiff_types.h"
#include "src/aiff/aiff_common.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aiff {

enum class AiffParserState {
    SearchForm,
    ReadChunkHeader,
    ReadCommPayload,
    ReadFverPayload,
    ReadSsndHeader,
    SkipSsndOffset,
    SkipChunkPayload,
    DataReady,
    Error
};

class AiffParser {
public:
    AiffParser();
    void reset();

    // Stream-oriented chunk scanner. Returns true if stream is valid, false on corruption.
    bool process_bytes(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed);

    bool is_header_complete() const { return state_ == AiffParserState::DataReady; }
    bool has_error() const { return state_ == AiffParserState::Error; }

    AiffFormType        get_form_type() const { return form_type_; }
    AiffCompressionType get_compression_type() const { return compression_type_; }
    AiffSampleFormat    get_sample_format() const { return sample_format_; }
    uint32_t            get_sample_rate() const { return sample_rate_; }
    uint16_t            get_channels() const { return channels_; }
    uint16_t            get_bits_per_sample() const { return bits_per_sample_; }
    uint32_t            get_total_frames() const { return total_frames_; }
    uint32_t            get_ssnd_offset() const { return ssnd_offset_; }
    uint32_t            get_ssnd_block_size() const { return ssnd_block_size_; }
    uint32_t            get_data_chunk_size() const { return data_chunk_size_; }

private:
    AiffParserState state_{AiffParserState::SearchForm};
    uint8_t buffer_[64];
    size_t buf_pos_{0};

    uint32_t current_chunk_id_{0};
    uint32_t current_chunk_size_{0};
    uint32_t bytes_to_skip_{0};

    AiffFormType form_type_{AiffFormType::Aiff};
    AiffCompressionType compression_type_{AiffCompressionType::None};
    AiffSampleFormat sample_format_{AiffSampleFormat::Int16BE};

    uint32_t sample_rate_{0};
    uint16_t channels_{0};
    uint16_t bits_per_sample_{0};
    uint32_t total_frames_{0};
    uint32_t ssnd_offset_{0};
    uint32_t ssnd_block_size_{0};
    uint32_t data_chunk_size_{0};

    bool comm_parsed_{false};

    bool process_comm_chunk(size_t chunk_size);
};

} // namespace audio_codecs::aiff
