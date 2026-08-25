#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include "src/vorbis/vorbis_codebook.h"
#include "src/vorbis/vorbis_floor.h"
#include "src/vorbis/vorbis_residue.h"
#include "src/vorbis/vorbis_mapping.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_codecs::vorbis {

struct VorbisMode {
    uint8_t blockflag{0}; // 0 = short blocksize_0, 1 = long blocksize_1
    uint16_t windowtype{0};
    uint16_t transformtype{0};
    uint8_t mapping{0};
};

struct VorbisSetup {
    uint32_t blocksize_0{512};
    uint32_t blocksize_1{2048};

    size_t codebook_count{0};
    VorbisCodebook codebooks[VORBIS_MAX_CODEBOOKS];

    size_t floor_count{0};
    VorbisFloor1Config floors[VORBIS_MAX_FLOORS];

    size_t residue_count{0};
    VorbisResidueConfig residues[VORBIS_MAX_RESIDUES];

    size_t mapping_count{0};
    VorbisMappingConfig mappings[VORBIS_MAX_MAPPINGS];

    size_t mode_count{0};
    VorbisMode modes[VORBIS_MAX_MODES];
};

// Check if packet is a Vorbis header of expected type
bool is_vorbis_header(const uint8_t* in_packet, size_t in_bytes, uint8_t expected_type);

// Parse Identification Header (type 0x01)
bool parse_vorbis_id_header(const uint8_t* in_packet, size_t in_bytes, VorbisInfo& info);

// Parse Comment Header (type 0x03)
bool parse_vorbis_comment_header(const uint8_t* in_packet, size_t in_bytes, VorbisComment& comment);

// Parse Setup Header (type 0x05)
bool parse_vorbis_setup_header(const uint8_t* in_packet, size_t in_bytes, 
                               uint8_t channels, VorbisSetup& setup);

} // namespace audio_codecs::vorbis
