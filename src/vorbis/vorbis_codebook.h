#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include "src/vorbis/vorbis_bitstream.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_codecs::vorbis {

constexpr uint32_t VORBIS_CODEBOOK_SYNC = 0x564342; // "BCV"
constexpr size_t VORBIS_MAX_CODEBOOK_ENTRIES = 4096;
constexpr size_t VORBIS_MAX_CODEBOOK_NODES = VORBIS_MAX_CODEBOOK_ENTRIES * 2;

struct VorbisCodebook {
    uint32_t dimensions{0};
    uint32_t entries{0};
    std::vector<uint8_t> lengths;
    uint8_t lookup_type{0}; // 0 = none, 1 = implicit, 2 = explicit
    float min_value{0.0f};
    float delta_value{0.0f};
    uint8_t quant_bits{0};
    uint8_t sequence_p{0};
    uint32_t lookup_values{0};
    std::vector<uint32_t> quantlist;

    // Generated lookup and decode tables
    std::vector<float> valuelist;
    struct Node {
        int16_t left{-1};
        int16_t right{-1};
        int32_t entry{-1};
    };
    std::vector<Node> tree;
    int16_t tree_root{-1};

    // Encoder codeword tables
    std::vector<uint32_t> codewords;
};

// Calculate lookup_values for maptype 1 (greatest integer where lookup_values^dim <= entries)
uint32_t vorbis_book_maptype1_quantvals(uint32_t entries, uint32_t dimensions);

// Initialize Huffman binary decode tree and valuelist
bool vorbis_codebook_init_tables(VorbisCodebook& book);

// Unpack codebook from bitstream
bool vorbis_codebook_unpack(VorbisBitReader& reader, VorbisCodebook& book);

// Pack codebook into bitstream
void vorbis_codebook_pack(VorbisBitWriter& writer, const VorbisCodebook& book);

// Decode next entry index from bitstream using Huffman tree
int vorbis_book_decode(VorbisBitReader& reader, const VorbisCodebook& book);

// Decode vector of values (lookup_type > 0)
bool vorbis_book_decode_v(VorbisBitReader& reader, const VorbisCodebook& book, float* out_vec);

// Decode vector and add into output buffer with stride
bool vorbis_book_decode_vadd(VorbisBitReader& reader, const VorbisCodebook& book, 
                             float* out_vec, size_t offset, size_t step, size_t count);

// Find nearest codebook entry for vector quantization
int vorbis_book_find_best(const VorbisCodebook& book, const float* target_vec);

} // namespace audio_codecs::vorbis
