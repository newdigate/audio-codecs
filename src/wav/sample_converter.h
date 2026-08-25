#pragma once
#include "audio_codecs/wav/wav_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::wav {

size_t bytes_per_sample(WavSampleFormat fmt);

void decode_samples_to_float(const uint8_t* in_bytes, WavSampleFormat fmt, float* out_samples, size_t count);
void decode_samples_to_i16(const uint8_t* in_bytes, WavSampleFormat fmt, int16_t* out_samples, size_t count);
void decode_samples_to_i32(const uint8_t* in_bytes, WavSampleFormat fmt, int32_t* out_samples, size_t count);

void encode_samples_from_float(const float* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count);
void encode_samples_from_i16(const int16_t* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count);
void encode_samples_from_i32(const int32_t* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count);

} // namespace audio_codecs::wav
