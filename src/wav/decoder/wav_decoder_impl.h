#pragma once
#include "src/wav/decoder/wav_parser.h"
#include "src/wav/sample_converter.h"
#include <algorithm>
#include <new>

namespace audio_codecs::wav {

template <size_t MaxChannels>
struct WavDecoderBase<MaxChannels>::Impl {
    WavParser parser;
    size_t last_frame_bytes{0};
    AudioConfig config;
    bool initialized{false};
};

template <size_t MaxChannels>
WavDecoderBase<MaxChannels>::WavDecoderBase() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
}

template <size_t MaxChannels>
bool WavDecoderBase<MaxChannels>::init(const AudioConfig& config) {
    if (!impl_) return false;
    reset();
    impl_->config = config;
    impl_->initialized = true;
    return true;
}

template <size_t MaxChannels>
void WavDecoderBase<MaxChannels>::reset() {
    if (impl_) {
        impl_->parser.reset();
        impl_->last_frame_bytes = 0;
    }
}

template <size_t MaxChannels>
bool WavDecoderBase<MaxChannels>::parse_stream_header(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed) {
    if (!impl_) return false;
    bool ok = impl_->parser.parse_chunk_stream(in_data, in_bytes, bytes_consumed);
    if (!ok) return false;
    if (impl_->parser.channels() > MaxChannels) {
        return false;
    }
    return impl_->parser.is_header_complete();
}

template <size_t MaxChannels>
int WavDecoderBase<MaxChannels>::decode_frame(const uint8_t* in_data, size_t in_bytes, 
                                              float* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || !out_pcm || max_out_samples == 0) return 0;
    size_t ch = impl_->parser.channels();
    if (ch == 0) ch = impl_->config.channels;
    if (ch == 0 || ch > MaxChannels) return -1;

    size_t bps = bytes_per_sample(impl_->parser.sample_format());
    size_t frame_bytes = ch * bps;
    if (frame_bytes == 0) return -1;

    size_t frames_available = in_bytes / frame_bytes;
    size_t max_frames = max_out_samples / ch;
    size_t frames_to_decode = std::min(frames_available, max_frames);
    size_t samples_to_decode = frames_to_decode * ch;
    size_t bytes_to_decode = frames_to_decode * frame_bytes;

    impl_->last_frame_bytes = bytes_to_decode;
    if (samples_to_decode > 0) {
        decode_samples_to_float(in_data, impl_->parser.sample_format(), out_pcm, samples_to_decode);
    }
    return static_cast<int>(samples_to_decode);
}

template <size_t MaxChannels>
int WavDecoderBase<MaxChannels>::decode_frame_i16(const uint8_t* in_data, size_t in_bytes, 
                                                  int16_t* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || !out_pcm || max_out_samples == 0) return 0;
    size_t ch = impl_->parser.channels();
    if (ch == 0) ch = impl_->config.channels;
    if (ch == 0 || ch > MaxChannels) return -1;

    size_t bps = bytes_per_sample(impl_->parser.sample_format());
    size_t frame_bytes = ch * bps;
    if (frame_bytes == 0) return -1;

    size_t frames_available = in_bytes / frame_bytes;
    size_t max_frames = max_out_samples / ch;
    size_t frames_to_decode = std::min(frames_available, max_frames);
    size_t samples_to_decode = frames_to_decode * ch;
    size_t bytes_to_decode = frames_to_decode * frame_bytes;

    impl_->last_frame_bytes = bytes_to_decode;
    if (samples_to_decode > 0) {
        decode_samples_to_i16(in_data, impl_->parser.sample_format(), out_pcm, samples_to_decode);
    }
    return static_cast<int>(samples_to_decode);
}

template <size_t MaxChannels>
int WavDecoderBase<MaxChannels>::decode_frame_i32(const uint8_t* in_data, size_t in_bytes, 
                                                  int32_t* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || !out_pcm || max_out_samples == 0) return 0;
    size_t ch = impl_->parser.channels();
    if (ch == 0) ch = impl_->config.channels;
    if (ch == 0 || ch > MaxChannels) return -1;

    size_t bps = bytes_per_sample(impl_->parser.sample_format());
    size_t frame_bytes = ch * bps;
    if (frame_bytes == 0) return -1;

    size_t frames_available = in_bytes / frame_bytes;
    size_t max_frames = max_out_samples / ch;
    size_t frames_to_decode = std::min(frames_available, max_frames);
    size_t samples_to_decode = frames_to_decode * ch;
    size_t bytes_to_decode = frames_to_decode * frame_bytes;

    impl_->last_frame_bytes = bytes_to_decode;
    if (samples_to_decode > 0) {
        decode_samples_to_i32(in_data, impl_->parser.sample_format(), out_pcm, samples_to_decode);
    }
    return static_cast<int>(samples_to_decode);
}

template <size_t MaxChannels>
int WavDecoderBase<MaxChannels>::decode_frame_f32(const uint8_t* in_data, size_t in_bytes, 
                                                  float* out_pcm, size_t max_out_samples) {
    return decode_frame(in_data, in_bytes, out_pcm, max_out_samples);
}

template <size_t MaxChannels>
uint32_t WavDecoderBase<MaxChannels>::get_sample_rate() const {
    return impl_ ? (impl_->parser.sample_rate() != 0 ? impl_->parser.sample_rate() : impl_->config.sample_rate) : 0;
}

template <size_t MaxChannels>
uint8_t WavDecoderBase<MaxChannels>::get_channels() const {
    return impl_ ? (impl_->parser.channels() != 0 ? impl_->parser.channels() : impl_->config.channels) : 0;
}

template <size_t MaxChannels>
uint8_t WavDecoderBase<MaxChannels>::get_bit_depth() const {
    return impl_ ? impl_->parser.bits_per_sample() : 0;
}

template <size_t MaxChannels>
WavFormat WavDecoderBase<MaxChannels>::get_format_tag() const {
    return impl_ ? impl_->parser.format_tag() : WavFormat::Pcm;
}

template <size_t MaxChannels>
WavSampleFormat WavDecoderBase<MaxChannels>::get_sample_format() const {
    return impl_ ? impl_->parser.sample_format() : WavSampleFormat::Int16LE;
}

template <size_t MaxChannels>
uint32_t WavDecoderBase<MaxChannels>::get_channel_mask() const {
    return impl_ ? impl_->parser.channel_mask() : 0;
}

template <size_t MaxChannels>
uint64_t WavDecoderBase<MaxChannels>::get_total_samples() const {
    return impl_ ? impl_->parser.total_samples() : 0;
}

template <size_t MaxChannels>
size_t WavDecoderBase<MaxChannels>::get_last_frame_bytes() const {
    return impl_ ? impl_->last_frame_bytes : 0;
}

} // namespace audio_codecs::wav
