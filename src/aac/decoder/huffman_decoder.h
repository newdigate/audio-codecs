#pragma once

#include "src/core/bit_reader.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

// Standard AAC Huffman codebook numbers
constexpr int HCB_ZERO = 0;
constexpr int HCB_1 = 1;
constexpr int HCB_2 = 2;
constexpr int HCB_3 = 3;
constexpr int HCB_4 = 4;
constexpr int HCB_5 = 5;
constexpr int HCB_6 = 6;
constexpr int HCB_7 = 7;
constexpr int HCB_8 = 8;
constexpr int HCB_9 = 9;
constexpr int HCB_10 = 10;
constexpr int HCB_ESC = 11;
constexpr int HCB_SCALEFACTOR = 12;
constexpr int HCB_NOISE = 13;
constexpr int HCB_INTENSITY2 = 14;
constexpr int HCB_INTENSITY = 15;

// Maximum quant limit for escape sequences (ISO/IEC 14496-3: 8191)
constexpr int MAX_QUANT = 8191;

// Decodes a 4-tuple of spectral coefficients from reader using codebook 1..4.
// Out_quad receives 4 ints [w, x, y, z].
// Returns true on success, false on error or EOF.
bool decode_spectral_quad(core::BitReader& reader, int codebook, int* out_quad);

// Decodes a 2-tuple of spectral coefficients from reader using codebook 5..11.
// Out_pair receives 2 ints [y, z].
// If codebook == 11, expands escape sequences if value >= 16.
// Returns true on success, false on error or EOF.
bool decode_spectral_pair(core::BitReader& reader, int codebook, int* out_pair);

// Decodes count spectral coefficients.
// Count must be multiple of 4 for cb 1..4, multiple of 2 for cb 5..11.
// For codebook 0 (HCB_ZERO), sets out_spectral to 0 without reading bits.
// Returns true on success, false on error.
bool decode_spectral_data(core::BitReader& reader, int codebook, int* out_spectral, int count);

// Decodes a single scalefactor DPCM step delta (-60 to +60) using Table 4.128 scalefactor codebook.
// Out_delta receives the decoded step delta.
// Returns true on success, false on error.
bool decode_scalefactor_delta(core::BitReader& reader, int& out_delta);

// Helper for encoder: retrieve codeword and bit length for an index in codebook 1..12
bool get_huffman_code(int codebook, int index, uint32_t& code, uint8_t& len);

// Helper to get number of entries in a codebook (e.g. 81, 64, 169, 289, 121)
int get_codebook_size(int codebook);

// Helper to get dimension (4, 2, or 1) of a codebook
int get_codebook_dimension(int codebook);

// Helper to check if codebook is signed
bool is_codebook_signed(int codebook);

// Helper to get largest absolute value (LAV) of a codebook
int get_codebook_lav(int codebook);

// Decode raw codeword index from reader for given codebook
bool decode_huffman_index(core::BitReader& reader, int codebook, int& out_index);

} // namespace audio_codecs::aac
