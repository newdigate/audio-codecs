#pragma once
#include "src/wav/sample_converter.h"
#include "src/wav/wav_common.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace audio_codecs::wav {

static inline void write_le32(uint8_t* p, uint32_t val) {
    p[0] = static_cast<uint8_t>(val & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

static inline void write_le16(uint8_t* p, uint16_t val) {
    p[0] = static_cast<uint8_t>(val & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

template <size_t MaxChannels>
struct WavEncoderBase<MaxChannels>::Impl {
    WavEncoderConfig config;
    uint64_t total_samples_written{0};
    uint32_t total_bytes_written{0};
    bool initialized{false};
};

template <size_t MaxChannels>
WavEncoderBase<MaxChannels>::WavEncoderBase() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
}

template <size_t MaxChannels>
bool WavEncoderBase<MaxChannels>::init(const AudioConfig& config) {
    if (!impl_) return false;
    WavEncoderConfig wav_cfg;
    wav_cfg.core_config = config;
    wav_cfg.sample_format = WavSampleFormat::Int16LE;
    return init_wav(wav_cfg);
}

template <size_t MaxChannels>
bool WavEncoderBase<MaxChannels>::init_wav(const WavEncoderConfig& config) {
    if (!impl_) return false;
    if (config.core_config.channels == 0 || config.core_config.channels > MaxChannels) {
        return false;
    }
    if (config.core_config.sample_rate == 0) {
        return false;
    }
    reset();
    impl_->config = config;
    impl_->initialized = true;
    return true;
}

template <size_t MaxChannels>
void WavEncoderBase<MaxChannels>::reset() {
    if (impl_) {
        impl_->total_samples_written = 0;
        impl_->total_bytes_written = 0;
    }
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::write_stream_header(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !out_data) return 0;

    uint16_t channels = impl_->config.core_config.channels;
    uint32_t sample_rate = impl_->config.core_config.sample_rate;
    WavSampleFormat sfmt = impl_->config.sample_format;
    bool is_float = (sfmt == WavSampleFormat::Float32LE);
    bool extensible = impl_->config.use_extensible || (channels > 2) || (impl_->config.channel_mask != 0);

    uint16_t bits_per_sample = static_cast<uint16_t>(bytes_per_sample(sfmt) * 8);
    uint16_t block_align = static_cast<uint16_t>(channels * (bits_per_sample / 8));
    uint32_t bytes_per_sec = sample_rate * block_align;

    size_t header_size = 12; // "RIFF" <size> "WAVE"
    header_size += (extensible ? 48 : 24); // "fmt " chunk (8 header + 40 or 16 payload)
    if (is_float) {
        header_size += 12; // "fact" chunk (8 header + 4 payload)
    }
    header_size += 8; // "data" <size>

    if (max_out_bytes < header_size) {
        return 0;
    }

    uint8_t* p = out_data;

    // 1. RIFF header
    write_le32(p + 0, kFourCcRiff);
    write_le32(p + 4, static_cast<uint32_t>(header_size - 8)); // placeholder
    write_le32(p + 8, kFourCcWave);
    p += 12;

    // 2. fmt chunk
    write_le32(p + 0, kFourCcFmt);
    if (extensible) {
        write_le32(p + 4, 40);
        write_le16(p + 8, static_cast<uint16_t>(WavFormat::Extensible));
        write_le16(p + 10, channels);
        write_le32(p + 12, sample_rate);
        write_le32(p + 16, bytes_per_sec);
        write_le16(p + 20, block_align);
        write_le16(p + 22, bits_per_sample);
        write_le16(p + 24, 22); // cbSize = 22
        write_le16(p + 26, bits_per_sample); // wValidBitsPerSample

        uint32_t mask = impl_->config.channel_mask;
        if (mask == 0) {
            if (channels == 1) mask = SpeakerMask::FrontCenter;
            else if (channels == 2) mask = SpeakerMask::StereoMask;
            else if (channels == 6) mask = SpeakerMask::Surround51Mask;
            else mask = (1u << channels) - 1;
        }
        write_le32(p + 28, mask);

        const uint8_t* guid = is_float ? kGuidIeeeFloat : kGuidPcm;
        std::memcpy(p + 32, guid, 16);
        p += 48;
    } else {
        write_le32(p + 4, 16);
        uint16_t tag = static_cast<uint16_t>(WavFormat::Pcm);
        if (is_float) tag = static_cast<uint16_t>(WavFormat::IeeeFloat);
        else if (sfmt == WavSampleFormat::ALaw8) tag = static_cast<uint16_t>(WavFormat::ALaw);
        else if (sfmt == WavSampleFormat::MuLaw8) tag = static_cast<uint16_t>(WavFormat::MuLaw);

        write_le16(p + 8, tag);
        write_le16(p + 10, channels);
        write_le32(p + 12, sample_rate);
        write_le32(p + 16, bytes_per_sec);
        write_le16(p + 20, block_align);
        write_le16(p + 22, bits_per_sample);
        p += 24;
    }

    // 3. fact chunk (if float)
    if (is_float) {
        write_le32(p + 0, kFourCcFact);
        write_le32(p + 4, 4);
        write_le32(p + 8, 0); // placeholder sample length
        p += 12;
    }

    // 4. data chunk header
    write_le32(p + 0, kFourCcData);
    write_le32(p + 4, 0); // placeholder data size
    p += 8;

    return static_cast<int>(header_size);
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::finalize_header(uint8_t* header_ptr, uint32_t total_data_bytes) {
    if (!impl_ || !header_ptr) return 0;

    uint32_t offset = 12;
    uint32_t data_offset = 0;
    uint32_t fact_offset = 0;

    // Scan chunks to locate fact and data chunk positions
    while (offset + 8 <= 256) {
        uint32_t chunk_id = *reinterpret_cast<uint32_t*>(&header_ptr[offset]);
        uint32_t chunk_size = *reinterpret_cast<uint32_t*>(&header_ptr[offset + 4]);

        if (chunk_id == kFourCcFact) {
            fact_offset = offset;
            offset += 8 + chunk_size;
        } else if (chunk_id == kFourCcFmt) {
            offset += 8 + chunk_size;
        } else if (chunk_id == kFourCcData) {
            data_offset = offset;
            break;
        } else {
            offset += 8 + chunk_size;
        }
    }

    if (data_offset == 0) return 0;

    // Write data chunk size
    write_le32(header_ptr + data_offset + 4, total_data_bytes);

    // Update fact chunk if present
    if (fact_offset != 0) {
        uint16_t channels = impl_->config.core_config.channels;
        WavSampleFormat sfmt = impl_->config.sample_format;
        uint16_t block_align = static_cast<uint16_t>(channels * bytes_per_sample(sfmt));
        uint32_t sample_frames = block_align > 0 ? (total_data_bytes / block_align) : 0;
        write_le32(header_ptr + fact_offset + 8, sample_frames);
    }

    // Update RIFF size
    uint32_t total_file_size = (data_offset + 8 + total_data_bytes);
    write_le32(header_ptr + 4, total_file_size - 8);

    return static_cast<int>(data_offset + 8);
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::encode_frame(const float* in_pcm, size_t in_samples, 
                                              uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || in_samples == 0 || max_out_bytes == 0) return 0;

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t max_samples = max_out_bytes / bps;
    size_t samples_to_encode = std::min(in_samples, max_samples);
    size_t bytes_to_write = samples_to_encode * bps;

    if (samples_to_encode > 0) {
        encode_samples_from_float(in_pcm, impl_->config.sample_format, out_data, samples_to_encode);
        impl_->total_samples_written += (samples_to_encode / impl_->config.core_config.channels);
        impl_->total_bytes_written += static_cast<uint32_t>(bytes_to_write);
    }

    return static_cast<int>(bytes_to_write);
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::encode_frame_i16(const int16_t* in_pcm, size_t in_samples, 
                                                  uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || in_samples == 0 || max_out_bytes == 0) return 0;

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t max_samples = max_out_bytes / bps;
    size_t samples_to_encode = std::min(in_samples, max_samples);
    size_t bytes_to_write = samples_to_encode * bps;

    if (samples_to_encode > 0) {
        encode_samples_from_i16(in_pcm, impl_->config.sample_format, out_data, samples_to_encode);
        impl_->total_samples_written += (samples_to_encode / impl_->config.core_config.channels);
        impl_->total_bytes_written += static_cast<uint32_t>(bytes_to_write);
    }

    return static_cast<int>(bytes_to_write);
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::encode_frame_i32(const int32_t* in_pcm, size_t in_samples, 
                                                  uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || in_samples == 0 || max_out_bytes == 0) return 0;

    size_t bps = bytes_per_sample(impl_->config.sample_format);
    size_t max_samples = max_out_bytes / bps;
    size_t samples_to_encode = std::min(in_samples, max_samples);
    size_t bytes_to_write = samples_to_encode * bps;

    if (samples_to_encode > 0) {
        encode_samples_from_i32(in_pcm, impl_->config.sample_format, out_data, samples_to_encode);
        impl_->total_samples_written += (samples_to_encode / impl_->config.core_config.channels);
        impl_->total_bytes_written += static_cast<uint32_t>(bytes_to_write);
    }

    return static_cast<int>(bytes_to_write);
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::encode_frame_f32(const float* in_pcm, size_t in_samples, 
                                                  uint8_t* out_data, size_t max_out_bytes) {
    return encode_frame(in_pcm, in_samples, out_data, max_out_bytes);
}

template <size_t MaxChannels>
int WavEncoderBase<MaxChannels>::flush(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_) return 0;
    if ((impl_->total_bytes_written & 1) && out_data && max_out_bytes >= 1) {
        out_data[0] = 0;
        return 1;
    }
    return 0;
}

template <size_t MaxChannels>
uint32_t WavEncoderBase<MaxChannels>::get_sample_rate() const {
    return impl_ ? impl_->config.core_config.sample_rate : 0;
}

template <size_t MaxChannels>
uint8_t WavEncoderBase<MaxChannels>::get_channels() const {
    return impl_ ? impl_->config.core_config.channels : 0;
}

template <size_t MaxChannels>
uint8_t WavEncoderBase<MaxChannels>::get_bit_depth() const {
    return impl_ ? static_cast<uint8_t>(bytes_per_sample(impl_->config.sample_format) * 8) : 0;
}

template <size_t MaxChannels>
uint64_t WavEncoderBase<MaxChannels>::get_total_samples() const {
    return impl_ ? impl_->total_samples_written : 0;
}

} // namespace audio_codecs::wav
