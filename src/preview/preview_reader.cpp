#include "audio_codecs/preview/preview_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::preview {

PreviewReader::PreviewReader() {
    std::memset(&header_, 0, sizeof(header_));
}

bool PreviewReader::init(SeekableReader& preview_file) {
    file_ = nullptr;
    if (!preview_file.seek(0)) return false;

    if (preview_file.read(reinterpret_cast<uint8_t*>(&header_), sizeof(header_)) != sizeof(header_)) {
        return false;
    }

    if (header_.magic != APV_MAGIC || header_.version != APV_VERSION) {
        return false;
    }

    if (header_.channels == 0 || header_.channels > 2) {
        return false;
    }

    if (header_.bytes_per_chunk != (header_.channels == 2 ? 4 : 2)) {
        return false;
    }

    file_ = &preview_file;
    cached_lod_ = 0xFF;
    cached_chunk_start_ = 0;
    cached_chunk_count_ = 0;

    return true;
}

uint32_t PreviewReader::duration_ms() const { return header_.duration_ms; }
uint32_t PreviewReader::sample_rate() const { return header_.sample_rate; }
uint8_t  PreviewReader::channels() const { return header_.channels; }
uint64_t PreviewReader::total_frames() const { return header_.total_pcm_frames; }
const ApvHeader& PreviewReader::header() const { return header_; }

uint8_t PreviewReader::select_lod(float ms_per_point) const {
    if (header_.lod_count < 2 || header_.sample_rate == 0) return 0;

    float ms_lod0 = (static_cast<float>(header_.samples_per_base_chunk) * 1000.0f) / header_.sample_rate;
    float ms_lod1 = ms_lod0 * (header_.lods[1].downsample_ratio ? static_cast<float>(header_.lods[1].downsample_ratio) : 16.0f);
    float ms_lod2 = ms_lod0 * (header_.lods[2].downsample_ratio ? static_cast<float>(header_.lods[2].downsample_ratio) : 256.0f);

    if (header_.lod_count >= 3 && ms_per_point >= ms_lod2 && header_.lods[2].chunk_count > 0) {
        return 2;
    }
    if (ms_per_point >= ms_lod1 && header_.lods[1].chunk_count > 0) {
        return 1;
    }
    return 0;
}

bool PreviewReader::load_chunk(uint8_t lod_idx, uint32_t chunk_idx) {
    if (!file_ || lod_idx >= header_.lod_count) return false;
    const ApvLodDescriptor& lod = header_.lods[lod_idx];
    if (chunk_idx >= lod.chunk_count) return false;

    // Check if already present in cached sector buffer
    if (cached_lod_ == lod_idx &&
        chunk_idx >= cached_chunk_start_ &&
        chunk_idx < cached_chunk_start_ + cached_chunk_count_) {
        return true;
    }

    uint32_t bytes_per_chunk = header_.bytes_per_chunk;
    if (bytes_per_chunk == 0) return false;

    uint32_t max_chunks = static_cast<uint32_t>(SECTOR_BUFFER_SIZE / bytes_per_chunk);
    uint32_t chunks_remaining = lod.chunk_count - chunk_idx;
    uint32_t chunks_to_read = std::min(max_chunks, chunks_remaining);
    size_t bytes_to_read = chunks_to_read * bytes_per_chunk;

    uint64_t file_offset = lod.file_offset + static_cast<uint64_t>(chunk_idx) * bytes_per_chunk;
    if (!file_->seek(file_offset)) return false;

    size_t bytes_read = file_->read(sector_buf_, bytes_to_read);
    if (bytes_read == 0) return false;

    cached_lod_ = lod_idx;
    cached_chunk_start_ = chunk_idx;
    cached_chunk_count_ = static_cast<uint32_t>(bytes_read / bytes_per_chunk);

    return (cached_chunk_count_ > 0);
}

bool PreviewReader::get_chunk_stereo(uint8_t lod_idx, uint32_t chunk_idx, WaveformPointStereo& out) {
    if (!load_chunk(lod_idx, chunk_idx)) return false;

    uint32_t idx_in_cache = chunk_idx - cached_chunk_start_;
    if (header_.channels == 2) {
        const auto* pts = reinterpret_cast<const WaveformPointStereo*>(sector_buf_);
        out = pts[idx_in_cache];
    } else {
        const auto* pts = reinterpret_cast<const WaveformPointMono*>(sector_buf_);
        out.left_min = pts[idx_in_cache].min;
        out.left_max = pts[idx_in_cache].max;
        out.right_min = pts[idx_in_cache].min;
        out.right_max = pts[idx_in_cache].max;
    }
    return true;
}

bool PreviewReader::get_chunk_mono(uint8_t lod_idx, uint32_t chunk_idx, WaveformPointMono& out) {
    if (!load_chunk(lod_idx, chunk_idx)) return false;

    uint32_t idx_in_cache = chunk_idx - cached_chunk_start_;
    if (header_.channels == 2) {
        const auto* pts = reinterpret_cast<const WaveformPointStereo*>(sector_buf_);
        out.min = std::min(pts[idx_in_cache].left_min, pts[idx_in_cache].right_min);
        out.max = std::max(pts[idx_in_cache].left_max, pts[idx_in_cache].right_max);
    } else {
        const auto* pts = reinterpret_cast<const WaveformPointMono*>(sector_buf_);
        out = pts[idx_in_cache];
    }
    return true;
}

size_t PreviewReader::read_preview_stereo(uint32_t start_ms, uint32_t duration_ms,
                                         WaveformPointStereo* out_points, size_t num_points) {
    if (!file_ || !out_points || num_points == 0 || header_.duration_ms == 0 || duration_ms == 0 || start_ms >= header_.duration_ms) {
        return 0;
    }

    float ms_per_point = static_cast<float>(duration_ms) / static_cast<float>(num_points);
    uint8_t lod_idx = select_lod(ms_per_point);
    if (lod_idx >= header_.lod_count) return 0;
    const ApvLodDescriptor& lod = header_.lods[lod_idx];
    if (lod.chunk_count == 0) return 0;

    float frames_per_chunk = static_cast<float>(header_.samples_per_base_chunk) * lod.downsample_ratio;
    if (header_.sample_rate == 0 || frames_per_chunk <= 0.0f) return 0;
    float ms_per_chunk = (frames_per_chunk * 1000.0f) / static_cast<float>(header_.sample_rate);
    if (ms_per_chunk <= 0.0f) return 0;

    float start_chunk_f = static_cast<float>(start_ms) / ms_per_chunk;
    float chunk_span = static_cast<float>(duration_ms) / ms_per_chunk;
    float chunks_per_point = chunk_span / static_cast<float>(num_points);

    for (size_t i = 0; i < num_points; ++i) {
        if (chunks_per_point >= 1.0f) {
            uint32_t c_start = static_cast<uint32_t>(start_chunk_f + static_cast<float>(i) * chunks_per_point);
            uint32_t c_end = static_cast<uint32_t>(start_chunk_f + static_cast<float>(i + 1) * chunks_per_point);
            if (c_start >= lod.chunk_count) c_start = lod.chunk_count - 1;
            if (c_end >= lod.chunk_count) c_end = lod.chunk_count - 1;
            if (c_end < c_start) c_end = c_start;

            int8_t l_min = 127, l_max = -128, r_min = 127, r_max = -128;
            bool any_read = false;
            for (uint32_t c = c_start; c <= c_end; ++c) {
                WaveformPointStereo pt{};
                if (get_chunk_stereo(lod_idx, c, pt)) {
                    if (pt.left_min < l_min) l_min = pt.left_min;
                    if (pt.left_max > l_max) l_max = pt.left_max;
                    if (pt.right_min < r_min) r_min = pt.right_min;
                    if (pt.right_max > r_max) r_max = pt.right_max;
                    any_read = true;
                }
            }
            if (!any_read) {
                l_min = 0; l_max = 0; r_min = 0; r_max = 0;
            }
            out_points[i] = {l_min, l_max, r_min, r_max};
        } else {
            float c_f = start_chunk_f + static_cast<float>(i) * chunks_per_point;
            uint32_t c0 = static_cast<uint32_t>(std::floor(c_f));
            uint32_t c1 = c0 + 1;
            float alpha = c_f - static_cast<float>(c0);

            if (c0 >= lod.chunk_count) c0 = lod.chunk_count - 1;
            if (c1 >= lod.chunk_count) c1 = lod.chunk_count - 1;

            WaveformPointStereo pt0{}, pt1{};
            get_chunk_stereo(lod_idx, c0, pt0);
            get_chunk_stereo(lod_idx, c1, pt1);

            int8_t l_min = static_cast<int8_t>(std::round((1.0f - alpha) * pt0.left_min + alpha * pt1.left_min));
            int8_t l_max = static_cast<int8_t>(std::round((1.0f - alpha) * pt0.left_max + alpha * pt1.left_max));
            int8_t r_min = static_cast<int8_t>(std::round((1.0f - alpha) * pt0.right_min + alpha * pt1.right_min));
            int8_t r_max = static_cast<int8_t>(std::round((1.0f - alpha) * pt0.right_max + alpha * pt1.right_max));

            if (l_min > l_max) l_min = l_max;
            if (r_min > r_max) r_min = r_max;

            out_points[i] = {l_min, l_max, r_min, r_max};
        }
    }
    return num_points;
}

size_t PreviewReader::read_preview(uint32_t start_ms, uint32_t duration_ms,
                                   WaveformPointMono* out_points, size_t num_points) {
    if (!file_ || !out_points || num_points == 0 || header_.duration_ms == 0 || duration_ms == 0 || start_ms >= header_.duration_ms) {
        return 0;
    }

    float ms_per_point = static_cast<float>(duration_ms) / static_cast<float>(num_points);
    uint8_t lod_idx = select_lod(ms_per_point);
    if (lod_idx >= header_.lod_count) return 0;
    const ApvLodDescriptor& lod = header_.lods[lod_idx];
    if (lod.chunk_count == 0) return 0;

    float frames_per_chunk = static_cast<float>(header_.samples_per_base_chunk) * lod.downsample_ratio;
    if (header_.sample_rate == 0 || frames_per_chunk <= 0.0f) return 0;
    float ms_per_chunk = (frames_per_chunk * 1000.0f) / static_cast<float>(header_.sample_rate);
    if (ms_per_chunk <= 0.0f) return 0;

    float start_chunk_f = static_cast<float>(start_ms) / ms_per_chunk;
    float chunk_span = static_cast<float>(duration_ms) / ms_per_chunk;
    float chunks_per_point = chunk_span / static_cast<float>(num_points);

    for (size_t i = 0; i < num_points; ++i) {
        if (chunks_per_point >= 1.0f) {
            uint32_t c_start = static_cast<uint32_t>(start_chunk_f + static_cast<float>(i) * chunks_per_point);
            uint32_t c_end = static_cast<uint32_t>(start_chunk_f + static_cast<float>(i + 1) * chunks_per_point);
            if (c_start >= lod.chunk_count) c_start = lod.chunk_count - 1;
            if (c_end >= lod.chunk_count) c_end = lod.chunk_count - 1;
            if (c_end < c_start) c_end = c_start;

            int8_t m_min = 127, m_max = -128;
            bool any_read = false;
            for (uint32_t c = c_start; c <= c_end; ++c) {
                WaveformPointMono pt{};
                if (get_chunk_mono(lod_idx, c, pt)) {
                    if (pt.min < m_min) m_min = pt.min;
                    if (pt.max > m_max) m_max = pt.max;
                    any_read = true;
                }
            }
            if (!any_read) {
                m_min = 0; m_max = 0;
            }
            out_points[i] = {m_min, m_max};
        } else {
            float c_f = start_chunk_f + static_cast<float>(i) * chunks_per_point;
            uint32_t c0 = static_cast<uint32_t>(std::floor(c_f));
            uint32_t c1 = c0 + 1;
            float alpha = c_f - static_cast<float>(c0);

            if (c0 >= lod.chunk_count) c0 = lod.chunk_count - 1;
            if (c1 >= lod.chunk_count) c1 = lod.chunk_count - 1;

            WaveformPointMono pt0{}, pt1{};
            get_chunk_mono(lod_idx, c0, pt0);
            get_chunk_mono(lod_idx, c1, pt1);

            int8_t m_min = static_cast<int8_t>(std::round((1.0f - alpha) * pt0.min + alpha * pt1.min));
            int8_t m_max = static_cast<int8_t>(std::round((1.0f - alpha) * pt0.max + alpha * pt1.max));

            if (m_min > m_max) m_min = m_max;

            out_points[i] = {m_min, m_max};
        }
    }
    return num_points;
}

} // namespace audio_codecs::preview
