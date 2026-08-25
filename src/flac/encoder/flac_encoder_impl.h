#pragma once
#include "audio_codecs/flac/flac_encoder.h"
#include "src/flac/flac_common.h"
#include "src/flac/crc.h"
#include "src/flac/md5.h"
#include "src/flac/metadata.h"
#include "src/flac/encoder/fixed_predictor.h"
#include "src/flac/encoder/lpc_analyzer.h"
#include "src/flac/encoder/rice_encoder.h"
#include "src/flac/encoder/channel_decorrelator.h"
#include "src/core/bit_writer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace audio_codecs::flac {

template <size_t MaxChannels, size_t MaxBlockSize>
struct FlacEncoderBase<MaxChannels, MaxBlockSize>::Impl {
    FlacEncoderConfig config{};
    uint64_t frame_count{0};
    uint64_t total_samples{0};
    Md5Context md5_ctx{};

    // Static channel buffers
    int32_t channel_in[MaxChannels][MaxBlockSize]{{0}};
    int32_t channel_proc[MaxChannels][MaxBlockSize]{{0}};
    int32_t residual_buf[MaxBlockSize]{0};
    int32_t best_residual[MaxBlockSize]{0};

    void reset() {
        frame_count = 0;
        total_samples = 0;
        md5_ctx.init();
    }

    static void write_utf8_number(core::BitWriter& writer, uint64_t val) {
        if (val < 0x80) {
            writer.write_bits(static_cast<uint32_t>(val), 8);
        } else if (val < 0x800) {
            writer.write_bits(static_cast<uint32_t>(0xC0 | (val >> 6)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | (val & 0x3F)), 8);
        } else if (val < 0x10000) {
            writer.write_bits(static_cast<uint32_t>(0xE0 | (val >> 12)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 6) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | (val & 0x3F)), 8);
        } else if (val < 0x200000) {
            writer.write_bits(static_cast<uint32_t>(0xF0 | (val >> 18)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 12) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 6) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | (val & 0x3F)), 8);
        } else if (val < 0x4000000) {
            writer.write_bits(static_cast<uint32_t>(0xF8 | (val >> 24)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 18) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 12) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 6) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | (val & 0x3F)), 8);
        } else if (val < 0x80000000) {
            writer.write_bits(static_cast<uint32_t>(0xFC | (val >> 30)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 24) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 18) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 12) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | ((val >> 6) & 0x3F)), 8);
            writer.write_bits(static_cast<uint32_t>(0x80 | (val & 0x3F)), 8);
        }
    }

    void encode_subframe(core::BitWriter& writer, 
                         const int32_t* samples, 
                         size_t block_size, 
                         uint8_t bps) {
        // Check for constant subframe
        bool is_constant = true;
        for (size_t i = 1; i < block_size; ++i) {
            if (samples[i] != samples[0]) {
                is_constant = false;
                break;
            }
        }

        if (is_constant) {
            // Header: 0 bit (1) + Constant type 000000 (6) + wasted flag 0 (1) = 0x00
            writer.write_bits(0, 1);
            writer.write_bits(0, 6);
            writer.write_bits(0, 1);
            writer.write_bits(static_cast<uint32_t>(samples[0]), bps);
            return;
        }

        // Check wasted bits
        uint32_t or_accum = 0;
        for (size_t i = 0; i < block_size; ++i) {
            or_accum |= static_cast<uint32_t>(samples[i]);
        }

        uint8_t wasted_bits = 0;
        if (or_accum != 0 && (or_accum & 1) == 0) {
            while ((or_accum & (1u << wasted_bits)) == 0 && wasted_bits < bps) {
                wasted_bits++;
            }
        }

        const int32_t* proc_samples = samples;
        int32_t shifted_samples[MaxBlockSize];
        if (wasted_bits > 0) {
            for (size_t i = 0; i < block_size; ++i) {
                shifted_samples[i] = samples[i] >> wasted_bits;
            }
            proc_samples = shifted_samples;
        }

        uint8_t subframe_bps = bps - wasted_bits;

        // Evaluate Fixed Predictor
        int best_fixed_order = FixedPredictor::find_best_fixed_order(proc_samples, block_size);
        FixedPredictor::compute_residual(proc_samples, block_size, best_fixed_order, residual_buf);

        uint8_t fixed_param = RiceEncoder::find_optimal_rice_param(&residual_buf[best_fixed_order], block_size - best_fixed_order, 4);

        // Estimate fixed bit size
        uint64_t fixed_bits = 0;
        for (size_t i = best_fixed_order; i < block_size; ++i) {
            uint32_t u = RiceEncoder::fold(residual_buf[i]);
            fixed_bits += (u >> fixed_param) + 1 + fixed_param;
        }

        bool use_lpc = false;
        int best_lpc_order = 0;
        int32_t qlp_coeff[32] = {0};
        int qlp_shift = 0;
        uint8_t lpc_param = 0;

        if (config.compression_level >= 3 && block_size > 16) {
            int max_lpc_order = std::min(32, 4 + (config.compression_level - 3) * 2);
            LpcAnalyzer::compute_lpc_coefficients(proc_samples, block_size, max_lpc_order, 
                                                 best_lpc_order, qlp_coeff, qlp_shift, 14);
            if (best_lpc_order > 0) {
                int32_t lpc_res[MaxBlockSize];
                for (int i = 0; i < best_lpc_order; ++i) {
                    lpc_res[i] = proc_samples[i];
                }
                for (size_t i = best_lpc_order; i < block_size; ++i) {
                    int64_t sum = 0;
                    for (int j = 0; j < best_lpc_order; ++j) {
                        sum += static_cast<int64_t>(qlp_coeff[j]) * proc_samples[i - 1 - j];
                    }
                    lpc_res[i] = proc_samples[i] - static_cast<int32_t>(sum >> qlp_shift);
                }

                lpc_param = RiceEncoder::find_optimal_rice_param(&lpc_res[best_lpc_order], block_size - best_lpc_order, 4);
                uint64_t lpc_bits = best_lpc_order * 14 + 9; // Coeffs + headers
                for (size_t i = best_lpc_order; i < block_size; ++i) {
                    uint32_t u = RiceEncoder::fold(lpc_res[i]);
                    lpc_bits += (u >> lpc_param) + 1 + lpc_param;
                }

                if (lpc_bits < fixed_bits) {
                    use_lpc = true;
                    std::memcpy(best_residual, lpc_res, block_size * sizeof(int32_t));
                }
            }
        }

        if (!use_lpc) {
            std::memcpy(best_residual, residual_buf, block_size * sizeof(int32_t));
        }

        // Write subframe header
        writer.write_bits(0, 1); // 0 bit
        if (use_lpc) {
            uint8_t type_code = 32 + (best_lpc_order - 1);
            writer.write_bits(type_code, 6);
        } else {
            uint8_t type_code = 8 + best_fixed_order;
            writer.write_bits(type_code, 6);
        }

        if (wasted_bits > 0) {
            writer.write_bits(1, 1);
            for (uint8_t z = 0; z < wasted_bits - 1; ++z) {
                writer.write_bits(0, 1);
            }
            writer.write_bits(1, 1);
        } else {
            writer.write_bits(0, 1);
        }

        int warmups = use_lpc ? best_lpc_order : best_fixed_order;
        for (int i = 0; i < warmups; ++i) {
            writer.write_bits(static_cast<uint32_t>(proc_samples[i]), subframe_bps);
        }

        if (use_lpc) {
            writer.write_bits(14 - 1, 4); // precision - 1 = 13 (14 bits)
            writer.write_bits(static_cast<uint32_t>(qlp_shift), 5);
            for (int i = 0; i < best_lpc_order; ++i) {
                writer.write_bits(static_cast<uint32_t>(qlp_coeff[i]), 14);
            }
        }

        // Residual coding header: method 0 (4-bit Rice), partition order 0
        writer.write_bits(0, 2);
        writer.write_bits(0, 4); // 1 partition

        uint8_t param = use_lpc ? lpc_param : fixed_param;
        RiceEncoder::encode_residual_partition(writer, &best_residual[warmups], block_size - warmups, 4, param);
    }
};

template <size_t MaxChannels, size_t MaxBlockSize>
FlacEncoderBase<MaxChannels, MaxBlockSize>::FlacEncoderBase() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for FlacEncoderBase::Impl");
    impl_ = new (state_buffer_) Impl();
}

template <size_t MaxChannels, size_t MaxBlockSize>
FlacEncoderBase<MaxChannels, MaxBlockSize>::~FlacEncoderBase() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

template <size_t MaxChannels, size_t MaxBlockSize>
bool FlacEncoderBase<MaxChannels, MaxBlockSize>::init(const AudioConfig& config) {
    FlacEncoderConfig flac_cfg;
    flac_cfg.core_config = config;
    flac_cfg.compression_level = 5;
    flac_cfg.block_size = 4096;
    flac_cfg.bit_depth = 16;
    return init_flac(flac_cfg);
}

template <size_t MaxChannels, size_t MaxBlockSize>
bool FlacEncoderBase<MaxChannels, MaxBlockSize>::init_flac(const FlacEncoderConfig& config) {
    reset();
    impl_->config = config;
    if (impl_->config.block_size == 0 || impl_->config.block_size > MaxBlockSize) {
        impl_->config.block_size = MaxBlockSize;
    }
    return true;
}

template <size_t MaxChannels, size_t MaxBlockSize>
void FlacEncoderBase<MaxChannels, MaxBlockSize>::reset() {
    if (impl_) {
        impl_->reset();
    }
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacEncoderBase<MaxChannels, MaxBlockSize>::write_stream_header(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !out_data || max_out_bytes < 42) return -1;

    FlacStreamInfo info;
    info.min_block_size = impl_->config.block_size;
    info.max_block_size = impl_->config.block_size;
    info.sample_rate = impl_->config.core_config.sample_rate;
    info.channels = impl_->config.core_config.channels;
    info.bits_per_sample = impl_->config.bit_depth;
    info.total_samples = 0; // Filled on finish_stream

    return static_cast<int>(MetadataBuilder::write_stream_header(out_data, max_out_bytes, info));
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacEncoderBase<MaxChannels, MaxBlockSize>::finish_stream(uint8_t* streaminfo_header_ptr) {
    if (!impl_ || !streaminfo_header_ptr) return -1;

    uint8_t md5_digest[16];
    impl_->md5_ctx.finish(md5_digest);

    if (MetadataBuilder::update_streaminfo(streaminfo_header_ptr, impl_->total_samples, md5_digest)) {
        return 0;
    }
    return -1;
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacEncoderBase<MaxChannels, MaxBlockSize>::flush(uint8_t* out_data, size_t max_out_bytes) {
    return 0; // FLAC frames are self-contained
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint32_t FlacEncoderBase<MaxChannels, MaxBlockSize>::get_sample_rate() const {
    return impl_ ? impl_->config.core_config.sample_rate : 44100;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint8_t FlacEncoderBase<MaxChannels, MaxBlockSize>::get_channels() const {
    return impl_ ? impl_->config.core_config.channels : 2;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint8_t FlacEncoderBase<MaxChannels, MaxBlockSize>::get_bit_depth() const {
    return impl_ ? impl_->config.bit_depth : 16;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint16_t FlacEncoderBase<MaxChannels, MaxBlockSize>::get_block_size() const {
    return impl_ ? impl_->config.block_size : 4096;
}

template <size_t MaxChannels, size_t MaxBlockSize>
uint64_t FlacEncoderBase<MaxChannels, MaxBlockSize>::get_total_samples() const {
    return impl_ ? impl_->total_samples : 0;
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacEncoderBase<MaxChannels, MaxBlockSize>::encode_frame_i32(const int32_t* in_pcm, size_t in_samples, 
                                                               uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || in_samples == 0 || !out_data || max_out_bytes < 16) {
        return -1;
    }

    uint8_t channels = impl_->config.core_config.channels;
    size_t block_size = in_samples / channels;
    if (block_size > MaxBlockSize || channels > MaxChannels) {
        return -2;
    }

    // De-interleave input samples
    for (size_t i = 0; i < block_size; ++i) {
        for (uint8_t ch = 0; ch < channels; ++ch) {
            impl_->channel_in[ch][i] = in_pcm[i * channels + ch];
        }
    }

    // Update MD5 hash
    uint8_t bps = impl_->config.bit_depth;
    for (size_t i = 0; i < block_size; ++i) {
        for (uint8_t ch = 0; ch < channels; ++ch) {
            int32_t val = impl_->channel_in[ch][i];
            uint8_t b[4];
            b[0] = static_cast<uint8_t>(val & 0xFF);
            b[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            b[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            b[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
            impl_->md5_ctx.update(b, bps / 8);
        }
    }
    impl_->total_samples += block_size;

    // Stereo decorrelation mode selection
    FlacChannelAssignment channel_mode = FlacChannelAssignment::Independent;
    if (channels == 2) {
        if constexpr (MaxChannels >= 2) {
            channel_mode = ChannelDecorrelatorEncoder::select_optimal_mode(impl_->channel_in[0], 
                                                                          impl_->channel_in[1], 
                                                                          block_size);
            ChannelDecorrelatorEncoder::apply_decorrelation(impl_->channel_in[0], 
                                                           impl_->channel_in[1], 
                                                           block_size, 
                                                           impl_->channel_proc[0], 
                                                           impl_->channel_proc[1], 
                                                           channel_mode);
        }
    } else {
        std::memcpy(impl_->channel_proc[0], impl_->channel_in[0], block_size * sizeof(int32_t));
    }

    // Start BitWriter on output buffer
    core::BitWriter writer;
    writer.init(out_data, max_out_bytes);

    // Frame Header (RFC 9639 Section 8.2)
    writer.write_bits(0x3FFE, 14); // Sync 0b11111111111110
    writer.write_bits(0, 1);       // Reserved 0
    writer.write_bits(0, 1);       // Fixed block size

    // Block size code
    uint8_t bs_code = 0;
    if (block_size == 192) bs_code = 1;
    else if (block_size == 576) bs_code = 2;
    else if (block_size == 1152) bs_code = 3;
    else if (block_size == 2304) bs_code = 4;
    else if (block_size == 4608) bs_code = 5;
    else if (block_size == 256) bs_code = 8;
    else if (block_size == 512) bs_code = 9;
    else if (block_size == 1024) bs_code = 10;
    else if (block_size == 2048) bs_code = 11;
    else if (block_size == 4096) bs_code = 12;
    else if (block_size == 8192) bs_code = 13;
    else if (block_size == 16384) bs_code = 14;
    else if (block_size == 32768) bs_code = 15;
    else bs_code = 7; // Uncommon 16-bit blocksize
    writer.write_bits(bs_code, 4);

    // Sample rate code
    uint32_t sr = impl_->config.core_config.sample_rate;
    uint8_t sr_code = 0;
    if (sr == 88200) sr_code = 1;
    else if (sr == 176400) sr_code = 2;
    else if (sr == 192000) sr_code = 3;
    else if (sr == 8000) sr_code = 4;
    else if (sr == 16000) sr_code = 5;
    else if (sr == 22050) sr_code = 6;
    else if (sr == 24000) sr_code = 7;
    else if (sr == 32000) sr_code = 8;
    else if (sr == 44100) sr_code = 9;
    else if (sr == 48000) sr_code = 10;
    else if (sr == 96000) sr_code = 11;
    else sr_code = 13; // Uncommon 16-bit sample rate
    writer.write_bits(sr_code, 4);

    // Channel assignment code
    uint8_t ch_code = (channel_mode == FlacChannelAssignment::Independent) ? (channels - 1) : static_cast<uint8_t>(channel_mode);
    writer.write_bits(ch_code, 4);

    // Bit depth code
    uint8_t ss_code = (bps == 8) ? 1 : ((bps == 12) ? 2 : ((bps == 16) ? 4 : ((bps == 20) ? 5 : ((bps == 24) ? 6 : ((bps == 32) ? 7 : 4)))));
    writer.write_bits(ss_code, 3);
    writer.write_bits(0, 1); // Reserved 0

    // Frame number (UTF-8)
    Impl::write_utf8_number(writer, impl_->frame_count++);

    if (bs_code == 7) {
        writer.write_bits(static_cast<uint32_t>(block_size - 1), 16);
    }
    if (sr_code == 13) {
        writer.write_bits(sr, 16);
    }

    // CRC-8 calculation on header
    size_t header_len = (writer.get_bit_count() + 7) / 8;
    uint8_t header_crc8 = crc8_calculate(out_data, header_len);
    writer.write_bits(header_crc8, 8);

    // Encode subframes
    for (uint8_t ch = 0; ch < channels; ++ch) {
        uint8_t sub_bps = bps;
        if ((channel_mode == FlacChannelAssignment::LeftSide && ch == 1) ||
            (channel_mode == FlacChannelAssignment::RightSide && ch == 0) ||
            (channel_mode == FlacChannelAssignment::MidSide && ch == 1)) {
            sub_bps += 1;
        }
        impl_->encode_subframe(writer, impl_->channel_proc[ch], block_size, sub_bps);
    }

    // Align to byte
    writer.flush_to_byte();

    // Frame CRC-16 calculation
    size_t frame_body_bytes = writer.get_byte_count();
    uint16_t frame_crc16 = crc16_calculate(out_data, frame_body_bytes);
    writer.write_bits(frame_crc16, 16);
    writer.flush_to_byte();

    return static_cast<int>(writer.get_byte_count());
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacEncoderBase<MaxChannels, MaxBlockSize>::encode_frame_i16(const int16_t* in_pcm, size_t in_samples, 
                                                               uint8_t* out_data, size_t max_out_bytes) {
    if (!in_pcm || in_samples == 0) return -1;

    int32_t i32_buf[MaxChannels * MaxBlockSize];
    for (size_t i = 0; i < in_samples && i < MaxChannels * MaxBlockSize; ++i) {
        i32_buf[i] = static_cast<int32_t>(in_pcm[i]);
    }

    return encode_frame_i32(i32_buf, in_samples, out_data, max_out_bytes);
}

template <size_t MaxChannels, size_t MaxBlockSize>
int FlacEncoderBase<MaxChannels, MaxBlockSize>::encode_frame(const float* in_pcm, size_t in_samples, 
                                                           uint8_t* out_data, size_t max_out_bytes) {
    if (!in_pcm || in_samples == 0) return -1;

    uint8_t bps = impl_->config.bit_depth;
    double scale = static_cast<double>(1u << (bps - 1)) - 1.0;

    int32_t i32_buf[MaxChannels * MaxBlockSize];
    for (size_t i = 0; i < in_samples && i < MaxChannels * MaxBlockSize; ++i) {
        float s = std::clamp(in_pcm[i], -1.0f, 1.0f);
        i32_buf[i] = static_cast<int32_t>(std::round(s * scale));
    }

    return encode_frame_i32(i32_buf, in_samples, out_data, max_out_bytes);
}

} // namespace audio_codecs::flac
