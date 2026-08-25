#pragma once
#include "src/aiff/decoder/aiff_parser.h"
#include "src/aiff/sample_converter.h"
#include <algorithm>
#include <new>

namespace audio_codecs::aiff {

template <size_t MaxChannels>
struct AiffDecoderBase<MaxChannels>::Impl {
    AiffParser parser;
    size_t last_frame_bytes{0};
    AudioConfig config;
    bool initialized{false};
};

template <size_t MaxChannels>
AiffDecoderBase<MaxChannels>::AiffDecoderBase() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
}

template <size_t MaxChannels>
bool AiffDecoderBase<MaxChannels>::init(const AudioConfig& config) {
    if (!impl_) return false;
    reset();
    impl_->config = config;
    impl_->initialized = true;
    return true;
}

template <size_t MaxChannels>
void AiffDecoderBase<MaxChannels>::reset() {
    if (impl_) {
        impl_->parser.reset();
        impl_->last_frame_bytes = 0;
    }
}

template <size_t MaxChannels>
bool AiffDecoderBase<MaxChannels>::parse_stream_header(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed) {
    if (!impl_) return false;
    bool ok = impl_->parser.process_bytes(in_data, in_bytes, bytes_consumed);
    if (!ok) return false;
    if (impl_->parser.get_channels() > MaxChannels) {
        return false;
    }
    return impl_->parser.is_header_complete();
}

template <size_t MaxChannels>
int AiffDecoderBase<MaxChannels>::decode_frame(const uint8_t* in_data, size_t in_bytes, 
                                               float* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || !out_pcm || max_out_samples == 0) return 0;
    size_t ch = impl_->parser.get_channels();
    if (ch == 0) ch = impl_->config.channels;
    if (ch == 0 || ch > MaxChannels) return -1;

    size_t bps = bytes_per_sample(impl_->parser.get_sample_format());
    size_t frame_bytes = ch * bps;
    if (frame_bytes == 0) return -1;

    size_t frames_available = in_bytes / frame_bytes;
    size_t max_frames = max_out_samples / ch;
    size_t frames_to_decode = std::min(frames_available, max_frames);
    size_t samples_to_decode = frames_to_decode * ch;
    size_t bytes_to_decode = frames_to_decode * frame_bytes;

    impl_->last_frame_bytes = bytes_to_decode;
    if (samples_to_decode > 0) {
        decode_samples_to_float(in_data, impl_->parser.get_sample_format(), out_pcm, samples_to_decode);
    }
    return static_cast<int>(samples_to_decode);
}

template <size_t MaxChannels>
int AiffDecoderBase<MaxChannels>::decode_frame_i16(const uint8_t* in_data, size_t in_bytes, 
                                                   int16_t* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || !out_pcm || max_out_samples == 0) return 0;
    size_t ch = impl_->parser.get_channels();
    if (ch == 0) ch = impl_->config.channels;
    if (ch == 0 || ch > MaxChannels) return -1;

    size_t bps = bytes_per_sample(impl_->parser.get_sample_format());
    size_t frame_bytes = ch * bps;
    if (frame_bytes == 0) return -1;

    size_t frames_available = in_bytes / frame_bytes;
    size_t max_frames = max_out_samples / ch;
    size_t frames_to_decode = std::min(frames_available, max_frames);
    size_t samples_to_decode = frames_to_decode * ch;
    size_t bytes_to_decode = frames_to_decode * frame_bytes;

    impl_->last_frame_bytes = bytes_to_decode;
    if (samples_to_decode > 0) {
        decode_samples_to_i16(in_data, impl_->parser.get_sample_format(), out_pcm, samples_to_decode);
    }
    return static_cast<int>(samples_to_decode);
}

template <size_t MaxChannels>
int AiffDecoderBase<MaxChannels>::decode_frame_i32(const uint8_t* in_data, size_t in_bytes, 
                                                   int32_t* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || !out_pcm || max_out_samples == 0) return 0;
    size_t ch = impl_->parser.get_channels();
    if (ch == 0) ch = impl_->config.channels;
    if (ch == 0 || ch > MaxChannels) return -1;

    size_t bps = bytes_per_sample(impl_->parser.get_sample_format());
    size_t frame_bytes = ch * bps;
    if (frame_bytes == 0) return -1;

    size_t frames_available = in_bytes / frame_bytes;
    size_t max_frames = max_out_samples / ch;
    size_t frames_to_decode = std::min(frames_available, max_frames);
    size_t samples_to_decode = frames_to_decode * ch;
    size_t bytes_to_decode = frames_to_decode * frame_bytes;

    impl_->last_frame_bytes = bytes_to_decode;
    if (samples_to_decode > 0) {
        decode_samples_to_i32(in_data, impl_->parser.get_sample_format(), out_pcm, samples_to_decode);
    }
    return static_cast<int>(samples_to_decode);
}

template <size_t MaxChannels>
int AiffDecoderBase<MaxChannels>::decode_frame_f32(const uint8_t* in_data, size_t in_bytes, 
                                                   float* out_pcm, size_t max_out_samples) {
    return decode_frame(in_data, in_bytes, out_pcm, max_out_samples);
}

template <size_t MaxChannels>
uint32_t AiffDecoderBase<MaxChannels>::get_sample_rate() const {
    if (!impl_) return 0;
    uint32_t sr = impl_->parser.get_sample_rate();
    return sr != 0 ? sr : impl_->config.sample_rate;
}

template <size_t MaxChannels>
uint8_t AiffDecoderBase<MaxChannels>::get_channels() const {
    if (!impl_) return 0;
    uint16_t ch = impl_->parser.get_channels();
    return ch != 0 ? static_cast<uint8_t>(ch) : impl_->config.channels;
}

template <size_t MaxChannels>
uint8_t AiffDecoderBase<MaxChannels>::get_bit_depth() const {
    if (!impl_) return 0;
    return static_cast<uint8_t>(impl_->parser.get_bits_per_sample());
}

template <size_t MaxChannels>
AiffFormType AiffDecoderBase<MaxChannels>::get_form_type() const {
    return impl_ ? impl_->parser.get_form_type() : AiffFormType::Aiff;
}

template <size_t MaxChannels>
AiffCompressionType AiffDecoderBase<MaxChannels>::get_compression_type() const {
    return impl_ ? impl_->parser.get_compression_type() : AiffCompressionType::None;
}

template <size_t MaxChannels>
AiffSampleFormat AiffDecoderBase<MaxChannels>::get_sample_format() const {
    return impl_ ? impl_->parser.get_sample_format() : AiffSampleFormat::Int16BE;
}

template <size_t MaxChannels>
uint64_t AiffDecoderBase<MaxChannels>::get_total_frames() const {
    return impl_ ? impl_->parser.get_total_frames() : 0;
}

template <size_t MaxChannels>
size_t AiffDecoderBase<MaxChannels>::get_last_frame_bytes() const {
    return impl_ ? impl_->last_frame_bytes : 0;
}

} // namespace audio_codecs::aiff
