#pragma once
#include <cstdint>
#include <cstddef>

namespace audio_codecs::aiff {

void uint32_to_ieee80(uint32_t sample_rate, uint8_t out[10]);
void double_to_ieee80(double value, uint8_t out[10]);
uint32_t ieee80_to_uint32(const uint8_t in[10]);
double ieee80_to_double(const uint8_t in[10]);

} // namespace audio_codecs::aiff
