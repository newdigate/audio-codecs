#pragma once
#include "audio_codecs/core/audio_types.h"

namespace audio_codecs {

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    virtual bool init(const AudioConfig& config) = 0;
    virtual void reset() = 0;
    virtual int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                             float* out_pcm, size_t max_out_samples) = 0;
};

} // namespace audio_codecs
