#pragma once
#include "audio_codecs/flac/flac_decoder.h"
#include "src/flac/flac_common.h"
#include "src/flac/crc.h"
#include "src/flac/metadata.h"
#include "src/flac/decoder/subframe_decoder.h"
#include "src/flac/decoder/channel_decorrelator.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace audio_codecs::flac {

template <size_t MaxChannels, size_t MaxBlockSize>
struct FlacDecoderBase<MaxChannels, MaxBlockSize>::Impl {
    FlacStreamInfo stream_info{};
    size_t last_frame_bytes{0};

    // Statically allocated channel buffers
    int32_t channel_samples[MaxChannels][MaxBlockSize]{{0}};
    int32_t scratch_residual[MaxBlockSize]{0};

    void reset() {
        last_frame_bytes = 0;
    }

    static bool parse_frame_header(core::BitReader& reader, 
                                   const uint8_t* frame_start, 
                                   const FlacStreamInfo& sinfo, 
                                   FlacFrameHeader& header) {
        if (reader.bits_remaining() < 32) return false;

        // 14 bits sync code
        uint32_t sync = reader.read_bits(14);
        if (sync != 0x3FFE) return false;

        // 1 bit reserved (0)
        uint32_t res0 = reader.read_bits(1);
        if (res0 != 0) return false;

        // 1 bit blocking strategy
        header.variable_block_size = (reader.read_bits(1) == 1);

        // 4 bits block size code
        uint32_t bs_code = reader.read_bits(4);
        if (bs_code == 0) return false;

        // 4 bits sample rate code
        uint32_t sr_code = reader.read_bits(4);
        if (sr_code == 15) return false;

        // 4 bits channel assignment
        uint32_t ch_code = reader.read_bits(4);
        if (ch_code >= 11) return false;

        // 3 bits sample size code
        uint32_t ss_code = reader.read_bits(3);
        if (ss_code == 3) return false;

        // 1 bit reserved (0)
        uint32_t res1 = reader.read_bits(1);
        if (res1 != 0) return false;

        // Channel assignment
        if (ch_code <= 7) {
            header.channel_assignment = FlacChannelAssignment::Independent;
            header.channels = static_cast<uint8_t>(ch_code + 1);
        } else if (ch_code == 8) {
            header.channel_assignment = FlacChannelAssignment::LeftSide;
            header.channels = 2;
        } else if (ch_code == 9) {
            header.channel_assignment = FlacChannelAssignment::RightSide;
            header.channels = 2;
        } else if (ch_code == 10) {
            header.channel_assignment = FlacChannelAssignment::MidSide;
            header.channels = 2;
        }

        // Bit depth
        static const uint8_t BPS_TABLE[8] = {0, 8, 12, 0, 16, 20, 24, 32};
        if (ss_code == 0) {
            header.bits_per_sample = sinfo.bits_per_sample ? sinfo.bits_per_sample : 16;
        } else {
            header.bits_per_sample = BPS_TABLE[ss_code];
        }

        // Block size
        if (bs_code == 1) {
            header.block_size = 192;
        } else if (bs_code >= 2 && bs_code <= 5) {
            header.block_size = static_cast<uint16_t>(576 * (1u << (bs_code - 2)));
        } else if (bs_code >= 8 && bs_code <= 15) {
            header.block_size = static_cast<uint16_t>(256 * (1u << (bs_code - 8)));
        }

        // Sample rate
        static const uint32_t SR_TABLE[16] = {
            0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, 0, 0, 0, 0
        };
        if (sr_code == 0) {
            header.sample_rate = sinfo.sample_rate ? sinfo.sample_rate : 44100;
        } else if (sr_code <= 11) {
            header.sample_rate = SR_TABLE[sr_code];
        }

        // UTF-8 coded frame / sample number
        uint32_t first_byte = reader.read_bits(8);
        uint64_t num = 0;
        if ((first_byte & 0x80) == 0) {
            num = first_byte;
        } else if ((first_byte & 0xE0) == 0xC0) {
            uint32_t b1 = reader.read_bits(8);
            num = ((first_byte & 0x1F) << 6) | (b1 & 0x3F);
        } else if ((first_byte & 0xF0) == 0xE0) {
            uint32_t b1 = reader.read_bits(8);
            uint32_t b2 = reader.read_bits(8);
            num = ((first_byte & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        } else if ((first_byte & 0xF8) == 0xF0) {
            uint32_t b1 = reader.read_bits(8);
            uint32_t b2 = reader.read_bits(8);
            uint32_t b3 = reader.read_bits(8);
            num = ((first_byte & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        } else if ((first_byte & 0xFC) == 0xF8) {
            uint32_t b1 = reader.read_bits(8);
            uint32_t b2 = reader.read_bits(8);
            uint32_t b3 = reader.read_bits(8);
            uint32_t b4 = reader.read_bits(8);
            num = (static_cast<uint64_t>(first_byte & 0x03) << 24) |
                  (static_cast<uint64_t>(b1 & 0x3F) << 18) |
                  (static_cast<uint64_t>(b2 & 0x3F) << 12) |
                  (static_cast<uint64_t>(b3 & 0x3F) << 6)  |
                  (b4 & 0x3F);
        } else if ((first_byte & 0xFE) == 0xFC) {
            uint32_t b1 = reader.read_bits(8);
            uint32_t b2 = reader.read_bits(8);
            uint32_t b3 = reader.read_bits(8);
            uint32_t b4 = reader.read_bits(8);
            uint32_t b5 = reader.read_bits(8);
            num = (static_cast<uint64_t>(first_byte & 0x01) << 30) |
                  (static_cast<uint64_t>(b1 & 0x3F) << 24) |
                  (static_cast<uint64_t>(b2 & 0x3F) << 18) |
                  (static_cast<uint64_t>(b3 & 0x3F) << 12) |
                  (static_cast<uint64_t>(b4 & 0x3F) << 6)  |
                  (b5 & 0x3F);
        } else if (first_byte == 0xFE) {
            uint32_t b1 = reader.read_bits(8);
            uint32_t b2 = reader.read_bits(8);
            uint32_t b3 = reader.read_bits(8);
            uint32_t b4 = reader.read_bits(8);
            uint32_t b5 = reader.read_bits(8);
            uint32_t b6 = reader.read_bits(8);
            num = (static_cast<uint64_t>(b1 & 0x3F) << 30) |
                  (static_cast<uint64_t>(b2 & 0x3F) << 24) |
                  (static_cast<uint64_t>(b3 & 0x3F) << 18) |
                  (static_cast<uint64_t>(b4 & 0x3F) << 12) |
                  (static_cast<uint64_t>(b5 & 0x3F) << 6)  |
                  (b6 & 0x3F);
        }
        header.frame_or_sample_number = num;

        // Uncommon block size
        if (bs_code == 6) {
            header.block_size = static_cast<uint16_t>(reader.read_bits(8) + 1);
        } else if (bs_code == 7) {
            header.block_size = static_cast<uint16_t>(reader.read_bits(16) + 1);
        }

        // Uncommon sample rate
        if (sr_code == 12) {
            header.sample_rate = reader.read_bits(8) * 1000;
        } else if (sr_code == 13) {
            header.sample_rate = reader.read_bits(16);
        } else if (sr_code == 14) {
            header.sample_rate = reader.read_bits(16) * 10;
        }

        // Header byte length up to CRC-8
        size_t header_len = (reader.get_position_bits() + 7) / 8;
        uint8_t expected_crc8 = crc8_calculate(frame_start, header_len);

        header.crc8 = static_cast<uint8_t>(reader.read_bits(8));
        if (header.crc8 != expected_crc8) {
            return false;
        }

        header.header_bytes = header_len + 1;
        return true;
    }
};

template <size_t MaxChannels, size_t MaxBlockSize>
FlacDecoderBase<MaxChannels, MaxBlockSize>::FlacDecoderBase() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for FlacDecoderBase::Impl");
    impl_ = new (state_buffer_) Impl();
}

template <size_t MaxChannels, size_t MaxBlockSize>
FlacDecoderBase<MaxChannels, MaxBlockSize>::~FlacDecoderBase() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

template <size_t MaxChannels, size_t MaxBlockSize>
bool FlacDecoderBase<MaxChannels, MaxBlockSize>::init(const AudioConfig& config) {
    reset();
    impl_->stream_info.sample_rate = config.sample_rate;
    impl_->stream_info.channels = config.channels;
    impl_->stream_info.bits_per_sample = 16;
    return true;
}

template <size_t MaxChannels, size_t MaxBlockSize>
void FlacDecoderBase<MaxChannels, MaxBlockSize>::reset() {
    if (impl_) {
        impl_->reset();
    }
}

template <size_t MaxChannels, size_t MaxBlockSize>
bool FlacDecoderBase<MaxChannels, MaxBlockSize>::parse_stream_header(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed) {
    if (!impl_ || !in_data) return false;
    return MetadataParser::parse_stream_header(in_data, in_bytes, impl_->stream_info, bytes_consumed);
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint32_t FlacDecoderBase<MaxChannels, MaxBlockSize>::get_sample_rate() const {
    return impl_ ? impl_->stream_info.sample_rate : 44100;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint8_t FlacDecoderBase<MaxChannels, MaxBlockSize>::get_channels() const {
    return impl_ ? impl_->stream_info.channels : 2;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint8_t FlacDecoderBase<MaxChannels, MaxBlockSize>::get_bit_depth() const {
    return impl_ ? impl_->stream_info.bits_per_sample : 16;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint64_t FlacDecoderBase<MaxChannels, MaxBlockSize>::get_total_samples() const {
    return impl_ ? impl_->stream_info.total_samples : 0;
}

template <size_t MaxChannels, size_t MaxBlockSize>
size_t FlacDecoderBase<MaxChannels, MaxBlockSize>::get_last_frame_bytes() const {
    return impl_ ? impl_->last_frame_bytes : 0;
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacDecoderBase<MaxChannels, MaxBlockSize>::decode_frame_i32(const uint8_t* in_data, size_t in_bytes, 
                                                               int32_t* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || in_bytes < 4 || !out_pcm) {
        return -1;
    }

    // Syncword search
    size_t sync_offset = 0;
    while (sync_offset + 4 <= in_bytes) {
        if (in_data[sync_offset] == 0xFF && (in_data[sync_offset + 1] & 0xFC) == 0xF8) {
            break;
        }
        sync_offset++;
    }

    if (sync_offset + 4 > in_bytes) {
        return -2; // Sync not found
    }

    const uint8_t* frame_ptr = in_data + sync_offset;
    size_t frame_remain = in_bytes - sync_offset;

    core::BitReader reader;
    reader.init(frame_ptr, frame_remain);

    FlacFrameHeader header;
    if (!Impl::parse_frame_header(reader, frame_ptr, impl_->stream_info, header)) {
        return -3; // Header parse error
    }

    if (header.channels > MaxChannels || header.block_size > MaxBlockSize) {
        return -4; // Exceeds statically allocated buffer bounds
    }

    // Decode subframes for each channel
    for (uint8_t ch = 0; ch < header.channels; ++ch) {
        uint8_t subframe_bps = header.bits_per_sample;
        // In side channels, bps is 1 bit larger
        if ((header.channel_assignment == FlacChannelAssignment::LeftSide && ch == 1) ||
            (header.channel_assignment == FlacChannelAssignment::RightSide && ch == 0) ||
            (header.channel_assignment == FlacChannelAssignment::MidSide && ch == 1)) {
            subframe_bps += 1;
        }

        if (!SubframeDecoder::decode_subframe(reader, 
                                              impl_->channel_samples[ch], 
                                              impl_->scratch_residual, 
                                              header.block_size, 
                                              subframe_bps)) {
            return -5; // Subframe decode error
        }
    }

    // Align reader to byte boundary
    size_t rem_bits = reader.get_position_bits() % 8;
    if (rem_bits != 0) {
        reader.skip_bits(8 - static_cast<int>(rem_bits));
    }

    // Read CRC-16 footer
    if (reader.bits_remaining() < 16) {
        return -6; // Truncated frame
    }

    size_t frame_body_bytes = reader.get_position_bits() / 8;
    uint16_t expected_crc16 = crc16_calculate(frame_ptr, frame_body_bytes);
    uint16_t frame_crc16 = static_cast<uint16_t>(reader.read_bits(16));

    if (frame_crc16 != expected_crc16) {
        return -7; // CRC-16 mismatch
    }

    impl_->last_frame_bytes = sync_offset + frame_body_bytes + 2;
    impl_->stream_info.sample_rate = header.sample_rate;
    impl_->stream_info.channels = header.channels;
    impl_->stream_info.bits_per_sample = header.bits_per_sample;

    // Undo interchannel decorrelation if stereo
    if (header.channels == 2 && header.channel_assignment != FlacChannelAssignment::Independent) {
        if constexpr (MaxChannels >= 2) {
            ChannelDecorrelatorDecoder::undo_decorrelation(impl_->channel_samples[0], 
                                                          impl_->channel_samples[1], 
                                                          header.block_size, 
                                                          header.channel_assignment);
        }
    }

    // Interleave output samples
    size_t total_samples = static_cast<size_t>(header.block_size) * header.channels;
    if (total_samples > max_out_samples) {
        return -8; // Buffer too small
    }

    for (size_t i = 0; i < header.block_size; ++i) {
        for (uint8_t ch = 0; ch < header.channels; ++ch) {
            out_pcm[i * header.channels + ch] = impl_->channel_samples[ch][i];
        }
    }

    return static_cast<int>(total_samples);
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacDecoderBase<MaxChannels, MaxBlockSize>::decode_frame_i16(const uint8_t* in_data, size_t in_bytes, 
                                                               int16_t* out_pcm, size_t max_out_samples) {
    if (!out_pcm) return -1;

    int32_t i32_buf[MaxChannels * MaxBlockSize];
    int samples = decode_frame_i32(in_data, in_bytes, i32_buf, MaxChannels * MaxBlockSize);
    if (samples <= 0) return samples;

    if (static_cast<size_t>(samples) > max_out_samples) return -8;

    uint8_t bps = impl_->stream_info.bits_per_sample;
    if (bps <= 16) {
        int shift = 16 - bps;
        for (int i = 0; i < samples; ++i) {
            out_pcm[i] = static_cast<int16_t>(i32_buf[i] << shift);
        }
    } else {
        int shift = bps - 16;
        for (int i = 0; i < samples; ++i) {
            out_pcm[i] = static_cast<int16_t>(i32_buf[i] >> shift);
        }
    }

    return samples;
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacDecoderBase<MaxChannels, MaxBlockSize>::decode_frame(const uint8_t* in_data, size_t in_bytes, 
                                                           float* out_pcm, size_t max_out_samples) {
    if (!out_pcm) return -1;

    int32_t i32_buf[MaxChannels * MaxBlockSize];
    int samples = decode_frame_i32(in_data, in_bytes, i32_buf, MaxChannels * MaxBlockSize);
    if (samples <= 0) return samples;

    if (static_cast<size_t>(samples) > max_out_samples) return -8;

    uint8_t bps = impl_->stream_info.bits_per_sample;
    double scale = 1.0 / (static_cast<double>(1u << (bps - 1)));

    for (int i = 0; i < samples; ++i) {
        out_pcm[i] = static_cast<float>(i32_buf[i] * scale);
    }

    return samples;
}

} // namespace audio_codecs::flac
