#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

// M/S (Mid/Side) Stereo Decoding:
// Reconstructs Left = M + S and Right = M - S for active scalefactor bands.
// Parameters:
// - left_spec: Input M spectrum, output Left spectrum (length swb_offsets[num_swb])
// - right_spec: Input S spectrum, output Right spectrum (length swb_offsets[num_swb])
// - ms_used: Array of active flags per scalefactor band (length num_swb)
// - swb_offsets: Scalefactor band offset table (length num_swb + 1)
// - num_swb: Number of scalefactor bands
void apply_ms_stereo(float* left_spec,
                     float* right_spec,
                     const uint8_t* ms_used,
                     const int* swb_offsets,
                     size_t num_swb);

// M/S Stereo Decoding for EightShort window sequence (num_windows windows)
// Parameters:
// - left_spec: Input/output buffer (length num_windows * swb_offsets[num_swb])
// - right_spec: Input/output buffer (length num_windows * swb_offsets[num_swb])
// - ms_used: Array of active flags (length num_windows * num_swb)
// - swb_offsets: Scalefactor band offset table for short windows (length num_swb + 1)
// - num_swb: Number of scalefactor bands per window
// - num_windows: Number of short windows (typically 8)
void apply_ms_stereo_short(float* left_spec,
                           float* right_spec,
                           const uint8_t* ms_used,
                           const int* swb_offsets,
                           size_t num_swb,
                           size_t num_windows);

// Intensity Stereo Decoding:
// Reconstructs Right = sign * Left * 2^(-0.25 * is_pos) for intensity-coded bands.
// Parameters:
// - left_spec: Left spectrum (length swb_offsets[num_swb])
// - right_spec: Output buffer for reconstructed Right spectrum (length swb_offsets[num_swb])
// - is_pos: Scalefactor / intensity positions per band (length num_swb)
// - is_type: Intensity coding type per band (0 = none, 1/15 = in-phase, 2/14 = inverted)
// - swb_offsets: Scalefactor band offset table (length num_swb + 1)
// - num_swb: Number of scalefactor bands
void apply_intensity_stereo(const float* left_spec,
                            float* right_spec,
                            const int* is_pos,
                            const uint8_t* is_type,
                            const int* swb_offsets,
                            size_t num_swb);

// Intensity Stereo Decoding for EightShort window sequence
void apply_intensity_stereo_short(const float* left_spec,
                                  float* right_spec,
                                  const int* is_pos,
                                  const uint8_t* is_type,
                                  const int* swb_offsets,
                                  size_t num_swb,
                                  size_t num_windows);

// Perceptual Noise Substitution (PNS):
// Generates unit-variance pseudo-random noise scaled to target band energy:
// Target RMS amplitude = 2^(0.25 * (pns_energy - 100))
// Parameters:
// - spec: Output spectral buffer (length swb_offsets[num_swb])
// - pns_active: Array of flags indicating PNS bands (length num_swb)
// - pns_energy: Array of noise energy scalefactors (length num_swb)
// - swb_offsets: Scalefactor band offset table (length num_swb + 1)
// - num_swb: Number of scalefactor bands
// - rng_state: 32-bit linear congruential PRNG state
void apply_pns(float* spec,
               const uint8_t* pns_active,
               const int* pns_energy,
               const int* swb_offsets,
               size_t num_swb,
               uint32_t& rng_state);

// Perceptual Noise Substitution for EightShort window sequence
void apply_pns_short(float* spec,
                     const uint8_t* pns_active,
                     const int* pns_energy,
                     const int* swb_offsets,
                     size_t num_swb,
                     size_t num_windows,
                     uint32_t& rng_state);

} // namespace audio_codecs::aac
