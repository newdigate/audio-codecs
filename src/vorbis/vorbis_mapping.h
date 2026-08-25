#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include "src/vorbis/vorbis_bitstream.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::vorbis {

constexpr size_t VORBIS_MAX_SUBMAPS = 16;
constexpr size_t VORBIS_MAX_COUPLING_STEPS = 64;

struct VorbisMappingConfig {
    uint8_t submaps{1};
    uint8_t coupling_steps{0};
    uint8_t coupling_mag[VORBIS_MAX_COUPLING_STEPS]{0};
    uint8_t coupling_angle[VORBIS_MAX_COUPLING_STEPS]{0};
    uint8_t ch_mux[VORBIS_MAX_CHANNELS]{0};
    uint8_t submap_floor[VORBIS_MAX_SUBMAPS]{0};
    uint8_t submap_residue[VORBIS_MAX_SUBMAPS]{0};
};

// Unpack Mapping 0 configuration from setup header
bool vorbis_mapping_unpack(VorbisBitReader& reader, VorbisMappingConfig& cfg, uint8_t channels);

// Pack Mapping 0 configuration to setup header
void vorbis_mapping_pack(VorbisBitWriter& writer, const VorbisMappingConfig& cfg, uint8_t channels);

// Undo polar channel coupling: Magnitude & Angle -> Left & Right
void vorbis_mapping_decouple(const VorbisMappingConfig& cfg, float** ch_spectra, size_t n2);

// Apply polar channel coupling: Left & Right -> Magnitude & Angle
void vorbis_mapping_couple(const VorbisMappingConfig& cfg, float** ch_spectra, size_t n2);

} // namespace audio_codecs::vorbis
