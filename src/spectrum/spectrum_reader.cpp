#include "audio_codecs/spectrum/spectrum_reader.h"
#include <cstring>
#include <algorithm>

namespace audio_codecs::spectrum {

SpectrumReader::SpectrumReader() {
    std::memset(&header_, 0, sizeof(header_));
}

bool SpectrumReader::init(preview::SeekableReader& spectrum_source) {
    source_ = nullptr;
    cached_sector_idx_ = UINT64_MAX;
    cached_bytes_ = 0;
    std::memset(&header_, 0, sizeof(header_));

    if (!spectrum_source.seek(0)) return false;
    if (spectrum_source.read(reinterpret_cast<uint8_t*>(&header_), sizeof(header_)) != sizeof(header_)) {
        return false;
    }

    if (header_.magic != ASV_MAGIC || header_.version != ASV_VERSION) {
        return false;
    }

    if (header_.num_bands == 0 || header_.num_bands > 64) {
        return false;
    }

    if (header_.channels == 0 || header_.channels > 2) {
        return false;
    }

    if (header_.lod_count == 0 || header_.lod_count > 4) {
        return false;
    }

    source_ = &spectrum_source;
    return true;
}

uint8_t SpectrumReader::select_lod(uint32_t duration_ms, size_t target_frames) const {
    if (header_.lod_count < 2 || target_frames == 0) return 0;
    if (header_.lods[1].frame_count == 0) return 0;
    float time_per_pixel = static_cast<float>(duration_ms) / static_cast<float>(target_frames);
    if (time_per_pixel >= 90.0f) {
        return 1;
    }
    return 0;
}

bool SpectrumReader::read_frame(uint8_t lod_idx, uint32_t frame_idx, uint8_t* out_frame) {
    if (!source_ || lod_idx >= header_.lod_count) return false;
    const auto& lod = header_.lods[lod_idx];
    if (frame_idx >= lod.frame_count) return false;

    uint64_t frame_bytes = header_.num_bands;
    if (frame_bytes == 0 || frame_bytes > 64) return false;

    uint64_t file_offset = lod.file_offset + static_cast<uint64_t>(frame_idx) * frame_bytes;
    uint64_t sector_idx = file_offset / 512;
    uint64_t sector_offset = file_offset % 512;

    if (sector_idx != cached_sector_idx_) {
        if (!source_->seek(sector_idx * 512)) {
            cached_sector_idx_ = UINT64_MAX;
            cached_bytes_ = 0;
            return false;
        }
        size_t n = source_->read(sector_cache_, 512);
        if (n == 0) {
            cached_sector_idx_ = UINT64_MAX;
            cached_bytes_ = 0;
            return false;
        }
        cached_sector_idx_ = sector_idx;
        cached_bytes_ = n;
    }

    if (sector_offset + frame_bytes <= 512) {
        if (sector_offset + frame_bytes > cached_bytes_) {
            return false;
        }
        std::memcpy(out_frame, sector_cache_ + sector_offset, frame_bytes);
        return true;
    }

    // Straddles two sectors
    size_t first_part = 512 - sector_offset;
    if (sector_offset + first_part > cached_bytes_) {
        return false;
    }
    std::memcpy(out_frame, sector_cache_ + sector_offset, first_part);

    cached_sector_idx_ = sector_idx + 1;
    if (!source_->seek(cached_sector_idx_ * 512)) {
        cached_sector_idx_ = UINT64_MAX;
        cached_bytes_ = 0;
        return false;
    }
    size_t n2 = source_->read(sector_cache_, 512);
    cached_bytes_ = n2;
    if (n2 < frame_bytes - first_part) {
        cached_sector_idx_ = UINT64_MAX;
        cached_bytes_ = 0;
        return false;
    }
    std::memcpy(out_frame + first_part, sector_cache_, frame_bytes - first_part);
    return true;
}

size_t SpectrumReader::read_spectrum(uint32_t start_ms,
                                     uint32_t duration_ms,
                                     uint8_t* out_bands,
                                     size_t max_frames) {
    if (!source_ || !out_bands || max_frames == 0 || header_.duration_ms == 0 || duration_ms == 0) {
        return 0;
    }
    if (start_ms >= header_.duration_ms) {
        return 0;
    }

    if (duration_ms > header_.duration_ms - start_ms) {
        duration_ms = header_.duration_ms - start_ms;
    }

    uint8_t lod_idx = select_lod(duration_ms, max_frames);
    const auto& lod = header_.lods[lod_idx];
    if (lod.frame_count == 0) {
        return 0;
    }

    float ms_per_frame = static_cast<float>(header_.duration_ms) / static_cast<float>(lod.frame_count);
    if (ms_per_frame <= 0.0f) {
        return 0;
    }

    for (size_t out_idx = 0; out_idx < max_frames; ++out_idx) {
        float t_start = static_cast<float>(start_ms) + (static_cast<float>(out_idx) / static_cast<float>(max_frames)) * static_cast<float>(duration_ms);
        float t_end = static_cast<float>(start_ms) + (static_cast<float>(out_idx + 1) / static_cast<float>(max_frames)) * static_cast<float>(duration_ms);

        uint32_t f_start = static_cast<uint32_t>(t_start / ms_per_frame);
        uint32_t f_end = static_cast<uint32_t>(t_end / ms_per_frame);
        if (f_start >= lod.frame_count) f_start = lod.frame_count - 1;
        if (f_end >= lod.frame_count) f_end = lod.frame_count - 1;
        if (f_end < f_start) f_end = f_start;

        uint8_t* dest_col = out_bands + out_idx * header_.num_bands;
        std::memset(dest_col, 0, header_.num_bands);

        uint8_t temp[64];
        for (uint32_t f = f_start; f <= f_end; ++f) {
            if (read_frame(lod_idx, f, temp)) {
                for (size_t b = 0; b < header_.num_bands; ++b) {
                    dest_col[b] = std::max(dest_col[b], temp[b]);
                }
            }
        }
    }

    return max_frames;
}

} // namespace audio_codecs::spectrum
