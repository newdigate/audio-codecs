#include "src/wav/decoder/wav_parser.h"
#include <algorithm>
#include <cstring>

namespace audio_codecs::wav {

static inline uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8) |
          (static_cast<uint32_t>(p[2]) << 16) |
          (static_cast<uint32_t>(p[3]) << 24);
}

static inline uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
          (static_cast<uint16_t>(p[1]) << 8);
}

WavParser::WavParser() {
    reset();
}

void WavParser::reset() {
    state_ = ParserState::SearchRiff;
    buf_pos_ = 0;
    current_chunk_id_ = 0;
    current_chunk_size_ = 0;
    bytes_to_skip_ = 0;
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    block_align_ = 0;
    format_tag_ = WavFormat::Pcm;
    sample_format_ = WavSampleFormat::Int16LE;
    channel_mask_ = 0;
    total_samples_ = 0;
    data_chunk_size_ = 0;
    fmt_parsed_ = false;
}

bool WavParser::parse_chunk_stream(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed) {
    bytes_consumed = 0;
    if (!in_data || in_bytes == 0) return true;

    while (bytes_consumed < in_bytes) {
        if (state_ == ParserState::DataReady) {
            return true;
        }
        if (state_ == ParserState::Error) {
            return false;
        }

        if (state_ == ParserState::SearchRiff) {
            while (bytes_consumed < in_bytes && buf_pos_ < 12) {
                buffer_[buf_pos_++] = in_data[bytes_consumed++];
            }
            if (buf_pos_ == 12) {
                uint32_t riff = read_le32(&buffer_[0]);
                uint32_t wave = read_le32(&buffer_[8]);
                if ((riff != kFourCcRiff && riff != kFourCcRf64) || wave != kFourCcWave) {
                    state_ = ParserState::Error;
                    return false;
                }
                buf_pos_ = 0;
                state_ = ParserState::ReadChunkHeader;
            }
        } else if (state_ == ParserState::ReadChunkHeader) {
            while (bytes_consumed < in_bytes && buf_pos_ < 8) {
                buffer_[buf_pos_++] = in_data[bytes_consumed++];
            }
            if (buf_pos_ == 8) {
                current_chunk_id_ = read_le32(&buffer_[0]);
                current_chunk_size_ = read_le32(&buffer_[4]);
                buf_pos_ = 0;

                if (current_chunk_id_ == kFourCcFmt) {
                    if (current_chunk_size_ < 16) {
                        state_ = ParserState::Error;
                        return false;
                    }
                    state_ = ParserState::ReadFmtPayload;
                    bytes_to_skip_ = (current_chunk_size_ & 1); // accounts for odd pad byte
                } else if (current_chunk_id_ == kFourCcFact) {
                    state_ = ParserState::ReadFactPayload;
                    bytes_to_skip_ = (current_chunk_size_ & 1);
                } else if (current_chunk_id_ == kFourCcData) {
                    if (!fmt_parsed_) {
                        state_ = ParserState::Error;
                        return false;
                    }
                    data_chunk_size_ = current_chunk_size_;
                    if (total_samples_ == 0 && block_align_ > 0) {
                        total_samples_ = data_chunk_size_ / block_align_;
                    }
                    state_ = ParserState::DataReady;
                    return true;
                } else {
                    bytes_to_skip_ = current_chunk_size_ + (current_chunk_size_ & 1);
                    state_ = ParserState::SkipChunkPayload;
                }
            }
        } else if (state_ == ParserState::ReadFmtPayload) {
            size_t needed = std::min<size_t>(current_chunk_size_, 40);
            while (bytes_consumed < in_bytes && buf_pos_ < needed) {
                buffer_[buf_pos_++] = in_data[bytes_consumed++];
            }
            if (buf_pos_ == needed) {
                // If there are extra bytes beyond 40, or pad byte
                size_t total_to_consume = current_chunk_size_ + (current_chunk_size_ & 1);
                if (total_to_consume > needed) {
                    bytes_to_skip_ = static_cast<uint32_t>(total_to_consume - needed);
                    state_ = ParserState::SkipChunkPayload;
                } else {
                    state_ = ParserState::ReadChunkHeader;
                    buf_pos_ = 0;
                }
                if (!process_fmt_chunk()) {
                    state_ = ParserState::Error;
                    return false;
                }
                fmt_parsed_ = true;
            }
        } else if (state_ == ParserState::ReadFactPayload) {
            while (bytes_consumed < in_bytes && buf_pos_ < 4) {
                buffer_[buf_pos_++] = in_data[bytes_consumed++];
            }
            if (buf_pos_ == 4) {
                total_samples_ = read_le32(&buffer_[0]);
                size_t total_to_consume = current_chunk_size_ + (current_chunk_size_ & 1);
                if (total_to_consume > 4) {
                    bytes_to_skip_ = static_cast<uint32_t>(total_to_consume - 4);
                    state_ = ParserState::SkipChunkPayload;
                } else {
                    state_ = ParserState::ReadChunkHeader;
                    buf_pos_ = 0;
                }
            }
        } else if (state_ == ParserState::SkipChunkPayload) {
            size_t can_skip = std::min<size_t>(bytes_to_skip_, in_bytes - bytes_consumed);
            bytes_consumed += can_skip;
            bytes_to_skip_ -= static_cast<uint32_t>(can_skip);
            if (bytes_to_skip_ == 0) {
                buf_pos_ = 0;
                state_ = ParserState::ReadChunkHeader;
            }
        }
    }

    return true;
}

bool WavParser::process_fmt_chunk() {
    uint16_t raw_format = read_le16(&buffer_[0]);
    channels_ = static_cast<uint8_t>(read_le16(&buffer_[2]));
    sample_rate_ = read_le32(&buffer_[4]);
    // uint32_t avg_bytes_sec = read_le32(&buffer_[8]);
    block_align_ = read_le16(&buffer_[12]);
    bits_per_sample_ = static_cast<uint8_t>(read_le16(&buffer_[14]));

    if (channels_ == 0 || sample_rate_ == 0) {
        return false;
    }

    format_tag_ = static_cast<WavFormat>(raw_format);

    if (format_tag_ == WavFormat::Pcm) {
        if (bits_per_sample_ == 8) {
            sample_format_ = WavSampleFormat::Uint8;
        } else if (bits_per_sample_ == 16) {
            sample_format_ = WavSampleFormat::Int16LE;
        } else if (bits_per_sample_ == 24) {
            sample_format_ = WavSampleFormat::Int24LE;
        } else if (bits_per_sample_ == 32) {
            sample_format_ = WavSampleFormat::Int32LE;
        } else {
            return false;
        }
    } else if (format_tag_ == WavFormat::IeeeFloat) {
        if (bits_per_sample_ == 32) {
            sample_format_ = WavSampleFormat::Float32LE;
        } else {
            return false;
        }
    } else if (format_tag_ == WavFormat::ALaw) {
        sample_format_ = WavSampleFormat::ALaw8;
    } else if (format_tag_ == WavFormat::MuLaw) {
        sample_format_ = WavSampleFormat::MuLaw8;
    } else if (format_tag_ == WavFormat::Extensible) {
        if (buf_pos_ < 40) {
            return false;
        }
        // uint16_t cb_size = read_le16(&buffer_[16]);
        // uint16_t valid_bits = read_le16(&buffer_[18]);
        channel_mask_ = read_le32(&buffer_[20]);
        const uint8_t* subformat = &buffer_[24];

        if (std::memcmp(subformat, kGuidPcm, 16) == 0) {
            format_tag_ = WavFormat::Pcm;
            if (bits_per_sample_ == 8) {
                sample_format_ = WavSampleFormat::Uint8;
            } else if (bits_per_sample_ == 16) {
                sample_format_ = WavSampleFormat::Int16LE;
            } else if (bits_per_sample_ == 24) {
                sample_format_ = WavSampleFormat::Int24LE;
            } else if (bits_per_sample_ == 32) {
                sample_format_ = WavSampleFormat::Int32LE;
            } else {
                return false;
            }
        } else if (std::memcmp(subformat, kGuidIeeeFloat, 16) == 0) {
            format_tag_ = WavFormat::IeeeFloat;
            if (bits_per_sample_ == 32) {
                sample_format_ = WavSampleFormat::Float32LE;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

} // namespace audio_codecs::wav
