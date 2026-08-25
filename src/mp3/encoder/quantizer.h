#pragma once
#include "src/mp3/mp3_common.h"
#include "src/mp3/mp3_tables.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class Quantizer {
public:
    // Quantize 576 MDCT spectral lines xr[] into integer is[] to fit bit budget
    static void quantize_granule(const float* xr_576, 
                                 const float* mask_thresholds_22,
                                 const FrameHeader& header, 
                                 GranuleChannelInfo& gi, 
                                 ScalefactorData& sf, 
                                 int16_t* is_out_576, 
                                 size_t target_bits);
};

} // namespace audio_codecs::mp3
