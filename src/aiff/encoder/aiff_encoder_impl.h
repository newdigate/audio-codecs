#pragma once
#include "src/aiff/sample_converter.h"
#include "src/aiff/aiff_common.h"
#include "src/aiff/ieee80.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace audio_codecs::aiff {

template <size_t MaxChannels>
struct AiffEncoderBase<MaxChannels>::Impl {
    AiffEncoderConfig config;
    uint64_t total_samples_written{0};
    uint32_t total_bytes_written{0};
    size_t header_size{0};
    size_t comm_frames_offset{0};
    size_t ssnd_size_offset{0};
    bool initialized{false};
};

template <size_t MaxChannels>
AiffEncoderBase<MaxChannels>::AiffEncoderBase() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
}

template <size_t MaxChannels>
bool AiffEncoderBase<MaxChannels>::init(const AudioConfig& config) {
    if (!impl_) return false;
    AiffEncoderConfig aiff_cfg;
    aiff_cfg.core_config = config;
    aiff_cfg.form_type = AiffFormType::Aiff;
    aiff_cfg.compression_type = AiffCompressionType::None;
    aiff_cfg.sample_format = AiffSampleFormat::Int16BE;
    aiff_cfg.bits_per_sample = 16;
    return init_aiff(aiff_cfg);
}

template <size_t MaxChannels>
bool AiffEncoderBase<MaxChannels>::init_aiff(const AiffEncoderConfig& config) {
    if (!impl_) return false;
    if (config.core_config.channels == 0 || config.core_config.channels > MaxChannels) {
        return false;
    }
    if (config.core_config.sample_rate == 0) {
        return false;
    }
    reset();
    impl_->config = config;

    // Infer bits_per_sample from sample_format if not explicitly set
    switch (config.sample_format) {
        case AiffSampleFormat::Int8:
        case AiffSampleFormat::ALaw8:
        case AiffSampleFormat::MuLaw8:
            impl_->config.bits_per_sample = 8;
            break;
        case AiffSampleFormat::Int16BE:
        case AiffSampleFormat::Int16LE:
            impl_->config.bits_per_sample = 16;
            break;
        case AiffSampleFormat::Int24BE:
        case AiffSampleFormat::Int24LE:
            impl_->config.bits_per_sample = 24;
            break;
        case AiffSampleFormat::Int32BE:
        case AiffSampleFormat::Int32LE:
        case AiffSampleFormat::Float32BE:
        case AiffSampleFormat::Float32LE:
            impl_->config.bits_per_sample = 32;
            break;
    }

    // Automatically select AIFC if compression or non-standard format is requested
    if (config.sample_format == AiffSampleFormat::Int16LE ||
        config.sample_format == AiffSampleFormat::Int24LE ||
        config.sample_format == AiffSampleFormat::Int32LE) {
        impl_->config.form_type = AiffFormType::Aifc;
        impl_->config.compression_type = AiffCompressionType::Sowt;
    } else if (config.sample_format == AiffSampleFormat::Float32BE ||
               config.sample_format == AiffSampleFormat::Float32LE) {
        impl_->config.form_type = AiffFormType::Aifc;
        impl_->config.compression_type = AiffCompressionType::Fl32;
    } else if (config.sample_format == AiffSampleFormat::ALaw8) {
        impl_->config.form_type = AiffFormType::Aifc;
        impl_->config.compression_type = AiffCompressionType::ALaw;
    } else if (config.sample_format == AiffSampleFormat::MuLaw8) {
        impl_->config.form_type = AiffFormType::Aifc;
        impl_->config.compression_type = AiffCompressionType::MuLaw;
    }

    impl_->initialized = true;
    return true;
}

template <size_t MaxChannels>
void AiffEncoderBase<MaxChannels>::reset() {
    if (impl_) {
        impl_->total_samples_written = 0;
        impl_->total_bytes_written = 0;
        impl_->header_size = 0;
        impl_->comm_frames_offset = 0;
        impl_->ssnd_size_offset = 0;
    }
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::write_stream_header(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !out_data) return 0;

    bool is_aifc = (impl_->config.form_type == AiffFormType::Aifc);
    uint16_t channels = impl_->config.core_config.channels;
    uint32_t sample_rate = impl_->config.core_config.sample_rate;
    uint16_t bits_per_sample = impl_->config.bits_per_sample;

    // Compute header layout size
    size_t header_sz = 12; // FORM header
    if (is_aifc) {
        header_sz += 12; // FVER chunk (8 + 4)
    }

    // COMM chunk size
    size_t comm_payload_sz = 18;
    const char* comp_name = "";
    size_t comp_name_len = 0;
    size_t comp_pstring_sz = 0;

    if (is_aifc) {
        switch (impl_->config.compression_type) {
            case AiffCompressionType::Sowt:
                comp_name = "Little-endian ";
                comp_name_len = 14;
                break;
            case AiffCompressionType::Fl32:
            case AiffCompressionType::FL32:
                comp_name = "32-bit float  ";
                comp_name_len = 14;
                break;
            case AiffCompressionType::ALaw:
                comp_name = "A-law";
                comp_name_len = 5;
                break;
            case AiffCompressionType::MuLaw:
                comp_name = "u-law";
                comp_name_len = 5;
                break;
            case AiffCompressionType::In24:
                comp_name = "24-bit integer";
                comp_name_len = 14;
                break;
            case AiffCompressionType::In32:
                comp_name = "32-bit integer";
                comp_name_len = 14;
                break;
            default:
                comp_name = "not compressed";
                comp_name_len = 14;
                break;
        }
        // Pascal string: 1 length byte + comp_name_len + pad byte if (1 + comp_name_len) is odd
        size_t raw_pstring = 1 + comp_name_len;
        comp_pstring_sz = (raw_pstring + 1) & ~1;
        comm_payload_sz = 18 + 4 + comp_pstring_sz;
    }

    header_sz += 8 + comm_payload_sz; // COMM chunk
    header_sz += 16;                  // SSND chunk header (8 chunk hdr + 4 offset + 4 blockSize)

    if (max_out_bytes < header_sz) {
        return 0;
    }

    impl_->header_size = header_sz;
    uint8_t* p = out_data;

    // 1. FORM Header
    write_be32(p + 0, kFourCcForm);
    write_be32(p + 4, static_cast<uint32_t>(header_sz - 8)); // placeholder
    write_be32(p + 8, is_aifc ? kFourCcAifc : kFourCcAiff);
    p += 12;

    // 2. FVER Chunk (if AIFC)
    if (is_aifc) {
        write_be32(p + 0, kFourCcFver);
        write_be32(p + 4, 4);
        write_be32(p + 8, kAifcVersion1);
        p += 12;
    }

    // 3. COMM Chunk
    write_be32(p + 0, kFourCcComm);
    write_be32(p + 4, static_cast<uint32_t>(comm_payload_sz));
    write_be16(p + 8, channels);
    impl_->comm_frames_offset = (p + 10) - out_data;
    write_be32(p + 10, 0); // placeholder for numSampleFrames
    write_be16(p + 14, bits_per_sample);
    uint32_to_ieee80(sample_rate, p + 16);

    if (is_aifc) {
        write_be32(p + 26, static_cast<uint32_t>(impl_->config.compression_type));
        uint8_t* pstr = p + 30;
        pstr[0] = static_cast<uint8_t>(comp_name_len);
        std::memcpy(pstr + 1, comp_name, comp_name_len);
        if (comp_pstring_sz > 1 + comp_name_len) {
            pstr[1 + comp_name_len] = 0; // pad byte
        }
    }
    p += (8 + comm_payload_sz);

    // 4. SSND Chunk Header
    write_be32(p + 0, kFourCcSsnd);
    impl_->ssnd_size_offset = (p + 4) - out_data;
    write_be32(p + 4, 8); // placeholder (audio_data_size + 8)
    write_be32(p + 8, 0); // offset = 0
    write_be32(p + 12, 0); // blockSize = 0
    p += 16;

    return static_cast<int>(header_sz);
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::finalize_header(uint8_t* header_ptr, uint32_t total_data_bytes) {
    if (!impl_ || !header_ptr || impl_->header_size < 12) return 0;

    uint32_t padded_data = total_data_bytes + (total_data_bytes % 2);
    uint32_t form_size = static_cast<uint32_t>(impl_->header_size - 8) + padded_data;
    write_be32(header_ptr + 4, form_size);

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t frame_bytes = impl_->config.core_config.channels * bps;
    uint32_t num_frames = (frame_bytes > 0) ? (total_data_bytes / frame_bytes) : 0;

    if (impl_->comm_frames_offset > 0) {
        write_be32(header_ptr + impl_->comm_frames_offset, num_frames);
    }
    if (impl_->ssnd_size_offset > 0) {
        write_be32(header_ptr + impl_->ssnd_size_offset, total_data_bytes + 8);
    }

    return static_cast<int>(impl_->header_size);
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::encode_frame(const float* in_pcm, size_t in_samples, 
                                              uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || in_samples == 0 || max_out_bytes == 0) return 0;

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t max_samples = max_out_bytes / bps;
    size_t samples_to_encode = std::min(in_samples, max_samples);
    if (samples_to_encode == 0) return 0;

    encode_samples_from_float(in_pcm, impl_->config.sample_format, out_data, samples_to_encode);

    size_t bytes_produced = samples_to_encode * bps;
    impl_->total_samples_written += samples_to_encode;
    impl_->total_bytes_written += static_cast<uint32_t>(bytes_produced);

    return static_cast<int>(bytes_produced);
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::encode_frame_i16(const int16_t* in_pcm, size_t in_samples, 
                                                  uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || in_samples == 0 || max_out_bytes == 0) return 0;

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t max_samples = max_out_bytes / bps;
    size_t samples_to_encode = std::min(in_samples, max_samples);
    if (samples_to_encode == 0) return 0;

    encode_samples_from_i16(in_pcm, impl_->config.sample_format, out_data, samples_to_encode);

    size_t bytes_produced = samples_to_encode * bps;
    impl_->total_samples_written += samples_to_encode;
    impl_->total_bytes_written += static_cast<uint32_t>(bytes_produced);

    return static_cast<int>(bytes_produced);
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::encode_frame_i32(const int32_t* in_pcm, size_t in_samples, 
                                                  uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || in_samples == 0 || max_out_bytes == 0) return 0;

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t max_samples = max_out_bytes / bps;
    size_t samples_to_encode = std::min(in_samples, max_samples);
    if (samples_to_encode == 0) return 0;

    encode_samples_from_i32(in_pcm, impl_->config.sample_format, out_data, samples_to_encode);

    size_t bytes_produced = samples_to_encode * bps;
    impl_->total_samples_written += samples_to_encode;
    impl_->total_bytes_written += static_cast<uint32_t>(bytes_produced);

    return static_cast<int>(bytes_produced);
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::encode_frame_f32(const float* in_pcm, size_t in_samples, 
                                                  uint8_t* out_data, size_t max_out_bytes) {
    return encode_frame(in_pcm, in_samples, out_data, max_out_bytes);
}

template <size_t MaxChannels>
int AiffEncoderBase<MaxChannels>::flush(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_) return 0;
    if (impl_->total_bytes_written % 2 != 0) {
        if (out_data && max_out_bytes >= 1) {
            out_data[0] = 0;
            return 1;
        }
    }
    return 0;
}

template <size_t MaxChannels>
uint32_t AiffEncoderBase<MaxChannels>::get_sample_rate() const {
    return impl_ ? impl_->config.core_config.sample_rate : 0;
}

template <size_t MaxChannels>
uint8_t AiffEncoderBase<MaxChannels>::get_channels() const {
    return impl_ ? impl_->config.core_config.channels : 0;
}

template <size_t MaxChannels>
uint8_t AiffEncoderBase<MaxChannels>::get_bit_depth() const {
    return impl_ ? impl_->config.bits_per_sample : 0;
}

template <size_t MaxChannels>
uint64_t AiffEncoderBase<MaxChannels>::get_total_samples() const {
    return impl_ ? impl_->total_samples_written : 0;
}

template <size_t MaxChannels>
AiffFormType AiffEncoderBase<MaxChannels>::get_form_type() const {
    return impl_ ? impl_->config.form_type : AiffFormType::Aiff;
}

template <size_t MaxChannels>
AiffCompressionType AiffEncoderBase<MaxChannels>::get_compression_type() const {
    return impl_ ? impl_->config.compression_type : AiffCompressionType::None;
}

template <size_t MaxChannels>
AiffSampleFormat AiffEncoderBase<MaxChannels>::get_sample_format() const {
    return impl_ ? impl_->config.sample_format : AiffSampleFormat::Int16BE;
}

} // namespace audio_codecs::aiff
