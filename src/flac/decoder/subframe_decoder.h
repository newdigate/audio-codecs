#pragma once
#include "src/core/bit_reader.h"
#include "src/flac/flac_common.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class LpcPredictor {
public:
    // Restore samples from LPC residual (RFC 9639 Section 8.3.4)
    static void restore_samples(const int32_t* residual, 
                                size_t count, 
                                int order, 
                                const int32_t* qlp_coeff, 
                                int qlp_shift, 
                                int32_t* inout_samples);
};

class SubframeDecoder {
public:
    // Decode an entire subframe (Constant, Verbatim, Fixed, or LPC)
    static bool decode_subframe(core::BitReader& reader, 
                                int32_t* out_samples, 
                                int32_t* scratch_residual,
                                size_t block_size, 
                                uint8_t bps);
};

} // namespace audio_codecs::flac
