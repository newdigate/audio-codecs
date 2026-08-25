#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::vorbis {

// Integer log base 2 helper as defined by Vorbis I specification
inline uint32_t vorbis_ilog(uint32_t v) {
    uint32_t ret = 0;
    while (v > 0) {
        ret++;
        v >>= 1;
    }
    return ret;
}

// Vorbis 32-bit float unpacker
float vorbis_unpack_float32(uint32_t val);

// Vorbis 32-bit float packer
uint32_t vorbis_pack_float32(float val);

// Generate standard Vorbis sine-of-sine window of length N
void vorbis_generate_window(float* out_window, size_t n);

// Generate transition window between long and short blocks
void vorbis_generate_slope_window(float* out_window, size_t n_curr, size_t n_prev, size_t n_next);

// Vorbis spec lookup table for Floor 1 inverse dB curve synthesis
extern const float kFloor1InverseDbTable[256];

} // namespace audio_codecs::vorbis
