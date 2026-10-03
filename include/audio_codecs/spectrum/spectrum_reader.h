#pragma once
#include "spectrum_types.h"
#include "audio_codecs/preview/preview_stream.h"
#include <cstdint>
#include <cstddef>

namespace audio_codecs::spectrum {

class SpectrumReader {
public:
    SpectrumReader();

    bool init(preview::SeekableReader& spectrum_source);

    uint32_t duration_ms() const { return header_.duration_ms; }
    uint32_t sample_rate() const { return header_.sample_rate; }
    uint8_t  channels() const { return header_.channels; }
    uint8_t  num_bands() const { return header_.num_bands; }
    uint16_t hop_size() const { return header_.hop_size; }
    uint16_t lod_count() const { return header_.lod_count; }
    const AsvHeader& header() const { return header_; }

    uint8_t select_lod(uint32_t duration_ms, size_t target_frames) const;

    size_t read_spectrum(uint32_t start_ms,
                         uint32_t duration_ms,
                         uint8_t* out_bands,
                         size_t max_frames);

private:
    bool read_frame(uint8_t lod_idx, uint32_t frame_idx, uint8_t* out_frame);

    preview::SeekableReader* source_{nullptr};
    AsvHeader header_{};
    uint8_t sector_cache_[512]{};
    uint64_t cached_sector_idx_{UINT64_MAX};
    size_t cached_bytes_{0};
};

} // namespace audio_codecs::spectrum
