#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class LpcAnalyzer {
public:
    // Compute optimal LPC coefficients via autocorrelation and Levinson-Durbin recursion
    static void compute_lpc_coefficients(const int32_t* samples, 
                                         size_t count, 
                                         int max_order, 
                                         int& best_order, 
                                         int32_t* qlp_coeff, 
                                         int& qlp_shift, 
                                         int precision_bits = 14);
};

} // namespace audio_codecs::flac
