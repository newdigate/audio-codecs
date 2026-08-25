#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include "src/vorbis/vorbis_bitstream.h"
#include "src/vorbis/vorbis_codebook.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_codecs::vorbis {

constexpr size_t VORBIS_MAX_FLOOR1_PARTITIONS = 32;
constexpr size_t VORBIS_MAX_FLOOR1_POSTS = 65;

struct VorbisFloor1Config {
    uint8_t partitions{0};
    uint8_t partition_class[VORBIS_MAX_FLOOR1_PARTITIONS]{0};
    uint8_t class_dimensions[16]{0};
    uint8_t class_subclasses[16]{0};
    uint8_t class_masterbooks[16]{0};
    int16_t subclass_books[16][8]{{0}};
    uint8_t multiplier{1};
    uint8_t rangebits{0};

    std::vector<uint32_t> post_list;
    std::vector<uint32_t> sorted_indices;
    std::vector<uint32_t> low_neighbors;
    std::vector<uint32_t> high_neighbors;
};

// Calculate linear prediction point
inline int32_t vorbis_render_point(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x) {
    if (x1 == x0) return y0;
    int32_t dy = y1 - y0;
    int32_t adx = x1 - x0;
    int32_t err = dy * (x - x0);
    return y0 + (err / adx);
}

// Setup neighbors and sorted indices for Floor 1
void vorbis_floor1_setup_neighbors(VorbisFloor1Config& cfg);

// Unpack Floor 1 configuration from setup header
bool vorbis_floor1_unpack(VorbisBitReader& reader, VorbisFloor1Config& cfg);

// Pack Floor 1 configuration to setup header
void vorbis_floor1_pack(VorbisBitWriter& writer, const VorbisFloor1Config& cfg);

// Decode Floor 1 packet coefficients Y (returns false if nonzero_flag is 0 / silent)
bool vorbis_floor1_decode(VorbisBitReader& reader, const VorbisFloor1Config& cfg, 
                          const VorbisCodebook* books, size_t book_count, 
                          int32_t* out_y);

// Render Floor 1 curve into out_floor buffer of size N/2
void vorbis_floor1_render(const VorbisFloor1Config& cfg, const int32_t* y_vals, 
                          float* out_floor, size_t n2);

// Fit Floor 1 Y points from target spectrum for encoding
void vorbis_floor1_fit(const float* target_spectrum, size_t n2, 
                       const VorbisFloor1Config& cfg, int32_t* out_y);

// Pack Floor 1 audio packet data
void vorbis_floor1_encode(VorbisBitWriter& writer, const VorbisFloor1Config& cfg,
                          const VorbisCodebook* books, size_t book_count,
                          const int32_t* y_vals);

} // namespace audio_codecs::vorbis
