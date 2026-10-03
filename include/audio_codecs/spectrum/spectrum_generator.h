#pragma once
#include "spectrum_types.h"
#include "fft_backend.h"
#include "spectrum_filterbank.h"
#include "audio_codecs/preview/preview_stream.h"

namespace audio_codecs::spectrum {

enum class GeneratorStatus {
    Ready,
    ProcessingLOD0,
    ProcessingLOD1,
    Finalizing,
    Complete,
    ErrorSource,
    ErrorDest,
    ErrorBackend
};

class SpectrumGenerator {
public:
    SpectrumGenerator();

    bool init(preview::SeekableReader& pcm_source,
              preview::SeekableWriter& spectrum_dest,
              FftBackend& fft_backend,
              uint32_t sample_rate,
              uint8_t source_channels,
              bool downmix_to_mono = true,
              uint8_t num_bands = DEFAULT_NUM_BANDS);

    GeneratorStatus step(size_t frame_budget = 32);
    bool generate_all();

    float progress() const;
    GeneratorStatus status() const { return status_; }
    uint32_t lod0_frame_count() const { return lod0_frame_count_; }
    uint32_t lod1_frame_count() const { return lod1_frame_count_; }

private:
    preview::SeekableReader* source_{nullptr};
    preview::SeekableWriter* dest_{nullptr};
    FftBackend* backend_{nullptr};
    SpectrumFilterbank filterbank_;

    uint32_t sample_rate_{44100};
    uint8_t source_channels_{2};
    bool downmix_to_mono_{true};
    uint8_t num_bands_{DEFAULT_NUM_BANDS};

    GeneratorStatus status_{GeneratorStatus::Ready};

    int16_t pcm_window_[1024]{};
    int16_t windowed_pcm_[1024]{};
    float magnitudes_[512]{};
    uint8_t frame_bands_[64]{};

    uint64_t total_pcm_frames_read_{0};
    uint32_t lod0_frame_count_{0};
    uint32_t lod1_frame_count_{0};

    uint64_t lod0_offset_{128};
    uint64_t lod1_offset_{0};

    uint32_t lod1_current_group_{0};
};

} // namespace audio_codecs::spectrum
