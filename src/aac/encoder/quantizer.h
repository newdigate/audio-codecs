#pragma once

#include "include/audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class AacQuantizer {
public:
    AacQuantizer() = default;

    // Single coefficient forward quantization:
    // ix = sign(x) * int((|x| * 2^(-0.25 * (sf - 100)))^0.75 + 0.4054)
    static int quantize_single(float x, int sf);

    // Fast MCU Mode: single-pass computation of scalefactors from masking thresholds
    // and rate control adjustment
    void quantize_spectrum_fast(const float* in_spectral, 
                                const float* masking_thresholds, 
                                const int* swb_offsets, 
                                size_t num_swb, 
                                int* out_quant, 
                                int* out_scalefactors, 
                                int& out_global_gain, 
                                int target_bits = 0);

    // High Quality Mode: outer distortion loop + inner rate-budget loop
    void quantize_spectrum_hq(const float* in_spectral, 
                              const float* masking_thresholds, 
                              const int* swb_offsets, 
                              size_t num_swb, 
                              int* out_quant, 
                              int* out_scalefactors, 
                              int& out_global_gain, 
                              int target_bits);
};

} // namespace audio_codecs::aac
