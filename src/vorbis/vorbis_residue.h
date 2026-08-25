#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include "src/vorbis/vorbis_bitstream.h"
#include "src/vorbis/vorbis_codebook.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_codecs::vorbis {

constexpr size_t VORBIS_MAX_RESIDUE_CLASSIFICATIONS = 64;
constexpr size_t VORBIS_MAX_RESIDUE_STAGES = 8;

struct VorbisResidueConfig {
    uint8_t type{2}; // 0 = interleaved, 1 = non-interleaved, 2 = interleaved across channels
    uint32_t begin{0};
    uint32_t end{0};
    uint32_t partition_size{0};
    uint8_t classifications{0};
    uint8_t classbook{0};
    int16_t books[VORBIS_MAX_RESIDUE_CLASSIFICATIONS][VORBIS_MAX_RESIDUE_STAGES]{{0}};
};

// Unpack residue configuration from setup header
bool vorbis_residue_unpack(VorbisBitReader& reader, VorbisResidueConfig& cfg);

// Pack residue configuration to setup header
void vorbis_residue_pack(VorbisBitWriter& writer, const VorbisResidueConfig& cfg);

// Decode residue vectors into channel residue arrays
bool vorbis_residue_decode(VorbisBitReader& reader, const VorbisResidueConfig& cfg,
                           const VorbisCodebook* books, size_t book_count,
                           float** ch_residues, const bool* ch_nonzero, 
                           uint8_t ch_count, size_t n2);

// Encode residue vectors from channel residue arrays
void vorbis_residue_encode(VorbisBitWriter& writer, const VorbisResidueConfig& cfg,
                           const VorbisCodebook* books, size_t book_count,
                           float** ch_residues, const bool* ch_nonzero,
                           uint8_t ch_count, size_t n2);

} // namespace audio_codecs::vorbis
