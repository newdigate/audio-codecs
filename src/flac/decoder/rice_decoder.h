#pragma once
#include "src/core/bit_reader.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class RiceDecoder {
public:
    // Decode a single Rice partition (RFC 9639 Section 8.4)
    static bool decode_residual_partition(core::BitReader& reader, 
                                          int32_t* out_residual, 
                                          size_t count, 
                                          uint8_t rice_param_bits);

    // Decode an escaped partition (RFC 9639 Section 8.4.1)
    static bool decode_escaped_partition(core::BitReader& reader, 
                                         int32_t* out_residual, 
                                         size_t count);

    // Zigzag unfolding: u -> s
    static inline int32_t unfold(uint32_t u) {
        return (u & 1) ? -static_cast<int32_t>((u >> 1) + 1) : static_cast<int32_t>(u >> 1);
    }
};

} // namespace audio_codecs::flac
