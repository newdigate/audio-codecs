#pragma once

#include "include/audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

// Sampling frequency index lookups
// Standard AAC sampling frequencies:
// 0: 96000, 1: 88200, 2: 64000, 3: 48000, 4: 44100, 5: 32000,
// 6: 24000, 7: 22050, 8: 16000, 9: 12000, 10: 11025, 11: 8000, 12: 7350
int get_sampling_frequency_index(uint32_t sample_rate);
uint32_t get_sample_rate_from_index(int index);

// Scalefactor band offset tables (ISO/IEC 13818-7 / 14496-3)
// Returns pointer to array of (num_bands + 1) offsets starting with 0 and ending with 1024 or 128
const int* get_swb_offset_long(uint32_t sample_rate, size_t& num_bands);
const int* get_swb_offset_short(uint32_t sample_rate, size_t& num_bands);
const int* get_swb_offset_long_index(int sf_index, size_t& num_bands);
const int* get_swb_offset_short_index(int sf_index, size_t& num_bands);

// Window tables (returns pointer to static array of length 2048 or 256)
const float* get_sine_window_1024(); // Length 2048
const float* get_sine_window_128();  // Length 256
const float* get_kbd_window_1024();  // Length 2048 (alpha = 4.0)
const float* get_kbd_window_128();   // Length 256  (alpha = 6.0)

// Composite window generator / selector for window transitions
// Returns pointer to window data of length 2048 (for OnlyLong, LongStart, LongStop) or 256 (for EightShort)
const float* get_window(WindowSequence seq, WindowShape shape_prev, WindowShape shape_curr, size_t& length);

// Non-linear dequantization lookup table: |val|^(4/3)
// Uses static lookup table for 0 <= |val| <= 256, and std::pow for |val| > 256.
float dequant_pow43(int val);

} // namespace audio_codecs::aac
