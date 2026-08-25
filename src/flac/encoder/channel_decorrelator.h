#pragma once
#include "src/flac/flac_common.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class ChannelDecorrelatorEncoder {
public:
    // Apply stereo decorrelation (RFC 9639 Section 8.2.3)
    static void apply_decorrelation(const int32_t* left, 
                                    const int32_t* right, 
                                    size_t count, 
                                    int32_t* out_ch0, 
                                    int32_t* out_ch1, 
                                    FlacChannelAssignment mode);

    // Analyze left & right channels and select the mode with lowest residual energy
    static FlacChannelAssignment select_optimal_mode(const int32_t* left, 
                                                     const int32_t* right, 
                                                     size_t count);
};

} // namespace audio_codecs::flac
