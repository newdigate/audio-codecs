#include "src/aiff/decoder/aiff_parser.h"
#include "src/aiff/ieee80.h"
#include <algorithm>
#include <cstring>

namespace audio_codecs::aiff {

AiffParser::AiffParser() {
    reset();
}

void AiffParser::reset() {
    state_ = AiffParserState::SearchForm;
    buf_pos_ = 0;
    current_chunk_id_ = 0;
    current_chunk_size_ = 0;
    bytes_to_skip_ = 0;
    form_type_ = AiffFormType::Aiff;
    compression_type_ = AiffCompressionType::None;
    sample_format_ = AiffSampleFormat::Int16BE;
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    total_frames_ = 0;
    ssnd_offset_ = 0;
    ssnd_block_size_ = 0;
    data_chunk_size_ = 0;
    comm_parsed_ = false;
}

bool AiffParser::process_comm_chunk(size_t chunk_size) {
    if (chunk_size < 18) return false;

    channels_ = read_be16(&buffer_[0]);
    total_frames_ = read_be32(&buffer_[2]);
    bits_per_sample_ = read_be16(&buffer_[6]);
    sample_rate_ = ieee80_to_uint32(&buffer_[8]);

    if (channels_ == 0 || bits_per_sample_ == 0 || sample_rate_ == 0) {
        return false;
    }

    if (form_type_ == AiffFormType::Aifc && chunk_size >= 22) {
        compression_type_ = static_cast<AiffCompressionType>(read_be32(&buffer_[18]));
    } else {
        compression_type_ = AiffCompressionType::None;
    }

    if (form_type_ == AiffFormType::Aiff || compression_type_ == AiffCompressionType::None) {
        if (bits_per_sample_ <= 8) {
            sample_format_ = AiffSampleFormat::Int8;
        } else if (bits_per_sample_ <= 16) {
            sample_format_ = AiffSampleFormat::Int16BE;
        } else if (bits_per_sample_ <= 24) {
            sample_format_ = AiffSampleFormat::Int24BE;
        } else {
            sample_format_ = AiffSampleFormat::Int32BE;
        }
    } else if (compression_type_ == AiffCompressionType::Sowt) {
        if (bits_per_sample_ <= 16) {
            sample_format_ = AiffSampleFormat::Int16LE;
        } else if (bits_per_sample_ <= 24) {
            sample_format_ = AiffSampleFormat::Int24LE;
        } else {
            sample_format_ = AiffSampleFormat::Int32LE;
        }
    } else if (compression_type_ == AiffCompressionType::Fl32 || compression_type_ == AiffCompressionType::FL32) {
        sample_format_ = AiffSampleFormat::Float32BE;
    } else if (compression_type_ == AiffCompressionType::ALaw) {
        sample_format_ = AiffSampleFormat::ALaw8;
    } else if (compression_type_ == AiffCompressionType::MuLaw) {
        sample_format_ = AiffSampleFormat::MuLaw8;
    } else if (compression_type_ == AiffCompressionType::In24) {
        sample_format_ = AiffSampleFormat::Int24BE;
    } else if (compression_type_ == AiffCompressionType::In32) {
        sample_format_ = AiffSampleFormat::Int32BE;
    } else {
        // Default based on bit depth
        if (bits_per_sample_ <= 8) sample_format_ = AiffSampleFormat::Int8;
        else if (bits_per_sample_ <= 16) sample_format_ = AiffSampleFormat::Int16BE;
        else if (bits_per_sample_ <= 24) sample_format_ = AiffSampleFormat::Int24BE;
        else sample_format_ = AiffSampleFormat::Int32BE;
    }

    comm_parsed_ = true;
    return true;
}

bool AiffParser::process_bytes(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed) {
    bytes_consumed = 0;
    if (!in_data || in_bytes == 0) return true;

    while (bytes_consumed < in_bytes) {
        if (state_ == AiffParserState::DataReady) {
            return true;
        }
        if (state_ == AiffParserState::Error) {
            return false;
        }

        switch (state_) {
            case AiffParserState::SearchForm: {
                size_t needed = 12 - buf_pos_;
                size_t avail = in_bytes - bytes_consumed;
                size_t to_copy = std::min(needed, avail);
                std::memcpy(&buffer_[buf_pos_], in_data + bytes_consumed, to_copy);
                buf_pos_ += to_copy;
                bytes_consumed += to_copy;

                if (buf_pos_ == 12) {
                    uint32_t magic = read_be32(&buffer_[0]);
                    uint32_t type = read_be32(&buffer_[8]);
                    if (magic != kFourCcForm) {
                        state_ = AiffParserState::Error;
                        return false;
                    }
                    if (type == kFourCcAiff) {
                        form_type_ = AiffFormType::Aiff;
                    } else if (type == kFourCcAifc) {
                        form_type_ = AiffFormType::Aifc;
                    } else {
                        state_ = AiffParserState::Error;
                        return false;
                    }
                    state_ = AiffParserState::ReadChunkHeader;
                    buf_pos_ = 0;
                }
                break;
            }

            case AiffParserState::ReadChunkHeader: {
                size_t needed = 8 - buf_pos_;
                size_t avail = in_bytes - bytes_consumed;
                size_t to_copy = std::min(needed, avail);
                std::memcpy(&buffer_[buf_pos_], in_data + bytes_consumed, to_copy);
                buf_pos_ += to_copy;
                bytes_consumed += to_copy;

                if (buf_pos_ == 8) {
                    current_chunk_id_ = read_be32(&buffer_[0]);
                    current_chunk_size_ = read_be32(&buffer_[4]);
                    buf_pos_ = 0;

                    if (current_chunk_id_ == kFourCcComm) {
                        state_ = AiffParserState::ReadCommPayload;
                    } else if (current_chunk_id_ == kFourCcFver) {
                        state_ = AiffParserState::ReadFverPayload;
                    } else if (current_chunk_id_ == kFourCcSsnd) {
                        if (!comm_parsed_) {
                            state_ = AiffParserState::Error;
                            return false;
                        }
                        state_ = AiffParserState::ReadSsndHeader;
                    } else {
                        bytes_to_skip_ = (current_chunk_size_ + 1) & ~1;
                        state_ = (bytes_to_skip_ > 0) ? AiffParserState::SkipChunkPayload : AiffParserState::ReadChunkHeader;
                    }
                }
                break;
            }

            case AiffParserState::ReadCommPayload: {
                size_t target_size = std::min<size_t>(current_chunk_size_, sizeof(buffer_));
                size_t needed = target_size - buf_pos_;
                size_t avail = in_bytes - bytes_consumed;
                size_t to_copy = std::min(needed, avail);
                std::memcpy(&buffer_[buf_pos_], in_data + bytes_consumed, to_copy);
                buf_pos_ += to_copy;
                bytes_consumed += to_copy;

                if (buf_pos_ == target_size) {
                    if (!process_comm_chunk(current_chunk_size_)) {
                        state_ = AiffParserState::Error;
                        return false;
                    }
                    size_t padded_size = (current_chunk_size_ + 1) & ~1;
                    if (padded_size > target_size) {
                        bytes_to_skip_ = static_cast<uint32_t>(padded_size - target_size);
                        state_ = AiffParserState::SkipChunkPayload;
                    } else {
                        state_ = AiffParserState::ReadChunkHeader;
                    }
                    buf_pos_ = 0;
                }
                break;
            }

            case AiffParserState::ReadFverPayload: {
                size_t target_size = std::min<size_t>(current_chunk_size_, 4);
                size_t needed = target_size - buf_pos_;
                size_t avail = in_bytes - bytes_consumed;
                size_t to_copy = std::min(needed, avail);
                std::memcpy(&buffer_[buf_pos_], in_data + bytes_consumed, to_copy);
                buf_pos_ += to_copy;
                bytes_consumed += to_copy;

                if (buf_pos_ == target_size) {
                    size_t padded_size = (current_chunk_size_ + 1) & ~1;
                    if (padded_size > target_size) {
                        bytes_to_skip_ = static_cast<uint32_t>(padded_size - target_size);
                        state_ = AiffParserState::SkipChunkPayload;
                    } else {
                        state_ = AiffParserState::ReadChunkHeader;
                    }
                    buf_pos_ = 0;
                }
                break;
            }

            case AiffParserState::ReadSsndHeader: {
                size_t needed = 8 - buf_pos_;
                size_t avail = in_bytes - bytes_consumed;
                size_t to_copy = std::min(needed, avail);
                std::memcpy(&buffer_[buf_pos_], in_data + bytes_consumed, to_copy);
                buf_pos_ += to_copy;
                bytes_consumed += to_copy;

                if (buf_pos_ == 8) {
                    ssnd_offset_ = read_be32(&buffer_[0]);
                    ssnd_block_size_ = read_be32(&buffer_[4]);
                    data_chunk_size_ = (current_chunk_size_ >= 8) ? (current_chunk_size_ - 8) : 0;
                    buf_pos_ = 0;

                    if (ssnd_offset_ > 0) {
                        if (ssnd_offset_ <= data_chunk_size_) {
                            data_chunk_size_ -= ssnd_offset_;
                        }
                        bytes_to_skip_ = ssnd_offset_;
                        state_ = AiffParserState::SkipSsndOffset;
                    } else {
                        state_ = AiffParserState::DataReady;
                        return true;
                    }
                }
                break;
            }

            case AiffParserState::SkipSsndOffset: {
                size_t avail = in_bytes - bytes_consumed;
                size_t to_skip = std::min<size_t>(bytes_to_skip_, avail);
                bytes_to_skip_ -= static_cast<uint32_t>(to_skip);
                bytes_consumed += to_skip;

                if (bytes_to_skip_ == 0) {
                    state_ = AiffParserState::DataReady;
                    return true;
                }
                break;
            }

            case AiffParserState::SkipChunkPayload: {
                size_t avail = in_bytes - bytes_consumed;
                size_t to_skip = std::min<size_t>(bytes_to_skip_, avail);
                bytes_to_skip_ -= static_cast<uint32_t>(to_skip);
                bytes_consumed += to_skip;

                if (bytes_to_skip_ == 0) {
                    state_ = AiffParserState::ReadChunkHeader;
                    buf_pos_ = 0;
                }
                break;
            }

            default:
                state_ = AiffParserState::Error;
                return false;
        }
    }

    return true;
}

} // namespace audio_codecs::aiff
