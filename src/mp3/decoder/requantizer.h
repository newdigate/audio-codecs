#pragma once
#include "src/core/bit_reader.h"
#include "src/mp3/mp3_common.h"
#include "src/mp3/mp3_tables.h"

namespace audio_codecs::mp3 {

class Requantizer {
public:
    // Decode scalefactors from bitstream for one granule/channel
    static bool decode_scalefactors(core::BitReader& reader, 
                                   const FrameHeader& header, 
                                   const SideInfo& side, 
                                   int gr, int ch, 
                                   ScalefactorData& sf,
                                   size_t& part2_bits_read);

    // Requantize 576 quantized integer values is[] to floating point xr[]
    static void requantize_granule(const int16_t* is, 
                                   const ScalefactorData& sf, 
                                   const GranuleChannelInfo& gi, 
                                   const FrameHeader& header, 
                                   float* xr_576);

    // Reorder short blocks into subband frequency order
    static void reorder_short_blocks(float* xr_576, const FrameHeader& header);

    // Joint stereo processing (MS Stereo and Intensity Stereo)
    static void process_stereo(float* xr_left, float* xr_right, 
                               const GranuleChannelInfo& gi_left, 
                               const GranuleChannelInfo& gi_right, 
                               const FrameHeader& header);

    // Aliasing reduction butterflies (for long blocks)
    static void alias_reduction(float* xr_576, const GranuleChannelInfo& gi);
};

} // namespace audio_codecs::mp3
