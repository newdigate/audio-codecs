#pragma once
#include "src/core/bit_writer.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class RiceEncoder {
public:
    // Zigzag folding: s -> u
    static inline uint32_t fold(int32_t s) {
        return (s >= 0) ? (static_cast<uint32_t>(s) << 1) : (~static_cast<uint32_t>(s) << 1) | 1;
    }

    // Find optimal Rice parameter k
    static uint8_t find_optimal_rice_param(const int32_t* residual, size_t count, uint8_t rice_param_bits);

    // Encode a single Rice partition
    static void encode_residual_partition(core::BitWriter& writer, 
                                          const int32_t* residual, 
                                          size_t count, 
                                          uint8_t rice_param_bits, 
                                          uint8_t param);

    // Encode an escaped partition
    static void encode_escaped_partition(core::BitWriter& writer, 
                                         const int32_t* residual, 
                                         size_t count, 
                                         uint8_t bit_depth);
};

} // namespace audio_codecs::flac
