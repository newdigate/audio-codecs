#pragma once
#include "audio_codecs/preview/preview_types.h"
#include "audio_codecs/preview/preview_stream.h"
#include "audio_codecs/core/decoder_interface.h"
#include <algorithm>

namespace audio_codecs::preview {

enum class GeneratorStatus {
    Working,
    Complete,
    ErrorSource,
    ErrorDest
};

class PreviewGenerator {
public:
    PreviewGenerator();
    ~PreviewGenerator() = default;

    bool init(SeekableReader& audio_source,
              SeekableWriter& preview_dest,
              AudioDecoder* decoder = nullptr,
              uint32_t sample_rate = 44100,
              uint8_t channels = 2,
              bool stereo = true);

    GeneratorStatus step(size_t chunk_budget = 64);
    bool generate_all();
    float progress() const;
    const ApvHeader& header() const;

private:
    enum class State {
        Init,
        DecodeLOD0,
        GenerateLOD1,
        GenerateLOD2,
        FinalizeHeader,
        Done,
        Error
    };

    int8_t scale_pcm(int16_t sample) const {
        int v = sample >> 8;
        return static_cast<int8_t>(std::clamp(v, -128, 127));
    }

    SeekableReader* source_{nullptr};
    SeekableWriter* dest_{nullptr};
    AudioDecoder* decoder_{nullptr};

    ApvHeader header_{};
    State state_{State::Init};

    bool stereo_{true};
    uint32_t sample_rate_{44100};
    uint8_t channels_{2};

    uint64_t total_frames_processed_{0};
    uint32_t lod0_chunk_count_{0};
    uint32_t lod1_chunk_count_{0};
    uint32_t lod2_chunk_count_{0};

    // Buffer for 1 base chunk (128 frames)
    int16_t frame_buf_[BASE_CHUNK_FRAMES * 2]{0};
};

} // namespace audio_codecs::preview
