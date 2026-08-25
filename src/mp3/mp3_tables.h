#pragma once
#include "src/mp3/mp3_common.h"
#include <cstdint>
#include <cstddef>

namespace audio_codecs::mp3 {

struct HuffmanEntry {
    uint16_t code;
    uint8_t len;
    uint8_t x;
    uint8_t y;
};

struct HuffmanCodebook {
    uint8_t table_num;
    uint8_t linbits;
    uint8_t max_val;
    size_t num_entries;
    const HuffmanEntry* entries;
};

struct Count1Entry {
    uint8_t code;
    uint8_t len;
    uint8_t v;
    uint8_t w;
    uint8_t x;
    uint8_t y;
};

extern const HuffmanCodebook HUFFMAN_CODEBOOKS[32];
extern const Count1Entry COUNT1_TABLE_A[16];
extern const Count1Entry COUNT1_TABLE_B[16];

// Scalefactor band boundary arrays (monotonically increasing line indices, 0..576)
// Array of 23 line indices for long blocks: sfb_indices[sfb] is the start index, and [sfb+1] is the end index.
const uint16_t* get_scalefac_band_table_long(uint32_t sample_rate);
// Array of 14 line indices for short blocks (0..192): sfb_indices[sfb] start, [sfb+1] end.
const uint16_t* get_scalefac_band_table_short(uint32_t sample_rate);

// Synthesis window D[512]
extern const float D_SYNTHESIS_WINDOW[512];

// Analysis window C[512]
extern const float C_ANALYSIS_WINDOW[512];

// Preemphasis table (pretab) for 22 long scalefactor bands
extern const uint8_t PRETAB[22];

// Aliasing reduction coefficients
extern const float ALIAS_CS[8];
extern const float ALIAS_CA[8];

// MPEG-1 scalefactor bit lengths from scalefac_compress (Table 16)
extern const uint8_t SLEN1_MPEG1[16];
extern const uint8_t SLEN2_MPEG1[16];

} // namespace audio_codecs::mp3
