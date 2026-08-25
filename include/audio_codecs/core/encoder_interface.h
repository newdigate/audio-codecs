#pragma once
#include "audio_codecs/core/audio_types.h"

namespace audio_codecs {

class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;
    virtual bool init(const AudioConfig& config) = 0;
    virtual void reset() = 0;
    virtual int encode_frame(const float* in_pcm, size_t in_samples,
                             uint8_t* out_data, size_t max_out_bytes) = 0;
    virtual int flush(uint8_t* out_data, size_t max_out_bytes) = 0;
};

} // namespace audio_codecs
