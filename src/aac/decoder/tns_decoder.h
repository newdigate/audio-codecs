#pragma once

#include "include/audio_codecs/aac/aac_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

struct TnsFilter {
    uint8_t start_band{0};
    uint8_t stop_band{0};
    uint8_t order{0};
    uint8_t direction{0}; // 0 = upward, 1 = downward
    float coef[16]{0.0f};  // converted LPC coefficients from PARCOR
};

struct TnsData {
    uint8_t n_filt[8]{0};
    TnsFilter filters[8][4]; // up to 4 filters per window (long or 8 short)
};

// Decodes raw PARCOR (reflection) coefficients into LPC coefficients
// using lattice step-up recursion algorithm.
// Parameters:
// - order: filter order (typically 0..12)
// - coef_res: resolution (0 or 3 -> 3 bits, 1 or 4 -> 4 bits)
// - raw_coef: array of raw coefficient indices (length order)
// - lpc_coef: output LPC coefficients array (length order)
void decode_tns_coef(int order, int coef_res, const int* raw_coef, float* lpc_coef);

// Applies TNS all-pole synthesis filter to spectral data across frequency bins.
// Parameters:
// - spec: spectral coefficient buffer (1024 for long, 8x128 for EightShort)
// - tns: TnsData structure containing active filters
// - swb_offsets: scalefactor band offsets table
// - num_swb: number of scalefactor bands
// - seq: window sequence (OnlyLong, LongStart, EightShort, LongStop)
void apply_tns(float* spec,
               const TnsData& tns,
               const int* swb_offsets,
               size_t num_swb,
               WindowSequence seq);

} // namespace audio_codecs::aac
