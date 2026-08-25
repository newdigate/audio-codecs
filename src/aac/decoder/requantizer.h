#pragma once

#include "src/aac/aac_tables.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

// Requantizes spectral coefficients for a Long window (1024 spectral lines)
// Formula: X_inv = sign(X) * |X|^(4/3) * 2^(0.25 * (scalefactor - 100))
// Parameters:
// - quant_spectral: input quantized coefficients (length swb_offsets[num_swb])
// - scalefactors: array of scalefactor values per band (length num_swb)
// - swb_offsets: array of SWB start offsets (length num_swb + 1)
// - num_swb: number of scalefactor bands
// - out_float_spectral: output buffer for reconstructed float spectrum (length swb_offsets[num_swb])
void requantize_spectrum(const int* quant_spectral, 
                         const int* scalefactors, 
                         const int* swb_offsets, 
                         size_t num_swb, 
                         float* out_float_spectral);

// Requantizes spectral coefficients for an EightShort window sequence (8 windows x 128 lines = 1024)
// Parameters:
// - quant_spectral: input array of length num_windows * swb_offsets[num_swb]
// - scalefactors: array of scalefactors of length num_windows * num_swb
// - swb_offsets: array of short SWB start offsets of length num_swb + 1
// - num_swb: number of scalefactor bands per window (e.g., 14 or 15)
// - num_windows: number of windows (typically 8)
// - out_float_spectral: output buffer of length num_windows * swb_offsets[num_swb]
void requantize_short_spectrum(const int* quant_spectral, 
                               const int* scalefactors, 
                               const int* swb_offsets, 
                               size_t num_swb, 
                               size_t num_windows, 
                               float* out_float_spectral);

} // namespace audio_codecs::aac
