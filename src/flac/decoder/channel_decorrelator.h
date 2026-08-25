#pragma once
#include "src/flac/flac_common.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class ChannelDecorrelatorDecoder {
public:
    // Undo interchannel decorrelation in-place (RFC 9639 Section 8.2.3)
    static void undo_decorrelation(int32_t* ch0, int32_t* ch1, size_t count, FlacChannelAssignment mode);
};

} // namespace audio_codecs::flac
