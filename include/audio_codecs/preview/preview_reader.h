#pragma once
#include "audio_codecs/preview/preview_types.h"
#include "audio_codecs/preview/preview_stream.h"

namespace audio_codecs::preview {

class PreviewReader {
public:
    PreviewReader();
    ~PreviewReader() = default;

    bool init(SeekableReader& preview_file);

    uint32_t duration_ms() const;
    uint32_t sample_rate() const;
    uint8_t  channels() const;
    uint64_t total_frames() const;
    const ApvHeader& header() const;

    uint8_t select_lod(float ms_per_point) const;

    size_t read_preview(uint32_t start_ms, uint32_t duration_ms,
                        WaveformPointMono* out_points, size_t num_points);

    size_t read_preview_stereo(uint32_t start_ms, uint32_t duration_ms,
                               WaveformPointStereo* out_points, size_t num_points);

private:
    static constexpr size_t SECTOR_BUFFER_SIZE = 512;

    bool load_chunk(uint8_t lod_idx, uint32_t chunk_idx);
    bool get_chunk_stereo(uint8_t lod_idx, uint32_t chunk_idx, WaveformPointStereo& out);
    bool get_chunk_mono(uint8_t lod_idx, uint32_t chunk_idx, WaveformPointMono& out);

    SeekableReader* file_{nullptr};
    ApvHeader header_{};

    alignas(4) uint8_t sector_buf_[SECTOR_BUFFER_SIZE]{0};
    uint8_t cached_lod_{0xFF};
    uint32_t cached_chunk_start_{0};
    uint32_t cached_chunk_count_{0};
};

} // namespace audio_codecs::preview
