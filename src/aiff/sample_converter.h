#pragma once
#include "audio_codecs/aiff/aiff_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aiff {

size_t bytes_per_sample(AiffSampleFormat fmt);

void decode_samples_to_float(const uint8_t* in_bytes, AiffSampleFormat fmt, float* out_samples, size_t count);
void decode_samples_to_i16(const uint8_t* in_bytes, AiffSampleFormat fmt, int16_t* out_samples, size_t count);
void decode_samples_to_i32(const uint8_t* in_bytes, AiffSampleFormat fmt, int32_t* out_samples, size_t count);

void encode_samples_from_float(const float* in_samples, AiffSampleFormat fmt, uint8_t* out_bytes, size_t count);
void encode_samples_from_i16(const int16_t* in_samples, AiffSampleFormat fmt, uint8_t* out_bytes, size_t count);
void encode_samples_from_i32(const int32_t* in_samples, AiffSampleFormat fmt, uint8_t* out_bytes, size_t count);

} // namespace audio_codecs::aiff
