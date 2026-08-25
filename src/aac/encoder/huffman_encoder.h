#pragma once

#include "src/core/bit_writer.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

class AacHuffmanEncoder {
public:
    // Selects the optimal Huffman codebook (0..11) for a band of quantized coefficients
    static int find_best_codebook_for_band(const int* quant_band, int len);

    // Calculates the bit count required to encode a band with given codebook
    static size_t count_bits_spectral_band(const int* quant_band, int len, int codebook);

    // Encodes a single 4-tuple of spectral coefficients using codebooks 1..4
    static bool encode_spectral_quad(core::BitWriter& writer, int codebook, const int* quad);

    // Encodes a single 2-tuple of spectral coefficients using codebooks 5..11 (with escape encoding)
    static bool encode_spectral_pair(core::BitWriter& writer, int codebook, const int* pair);

    // Encodes a single scalefactor delta (-60..+60) using codebook 12
    static bool encode_scalefactor_delta(core::BitWriter& writer, int delta);

    // Encodes an entire band using the specified codebook
    static size_t encode_spectral_data_band(core::BitWriter& writer, int codebook, const int* quant, int len);

    // Groups per-band codebooks into contiguous sections
    static void build_sections(const int* band_codebooks, 
                               size_t num_swb, 
                               int* out_section_cb, 
                               int* out_section_len, 
                               size_t& out_num_sections);

    // Writes section data to bitstream and returns total bits written
    static size_t write_section_data(core::BitWriter& writer, 
                                     const int* section_codebooks, 
                                     const int* section_lengths, 
                                     size_t num_sections,
                                     bool is_short = false);

    // Writes DPCM scalefactor data to bitstream and returns total bits written
    static size_t write_scalefactor_data(core::BitWriter& writer, 
                                         int global_gain, 
                                         const int* scalefactors, 
                                         const int* section_codebooks, 
                                         size_t num_swb);

    // Writes quantized spectral coefficients to bitstream and returns total bits written
    static size_t write_spectral_data(core::BitWriter& writer, 
                                      const int* quant_spectral, 
                                      const int* section_codebooks, 
                                      const int* swb_offsets, 
                                      size_t num_swb);
};

} // namespace audio_codecs::aac
