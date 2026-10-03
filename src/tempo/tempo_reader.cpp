#include <audio_codecs/tempo/tempo_reader.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::tempo {

TempoReader::TempoReader() : reader_(nullptr) {
    std::memset(&header_, 0, sizeof(header_));
}

TempoReader::~TempoReader() = default;

bool TempoReader::init(preview::SeekableReader* att_reader) {
    reader_ = nullptr;
    std::memset(&header_, 0, sizeof(header_));
    tempo_points_.clear();

    if (!att_reader) return false;

    if (!att_reader->seek(0)) return false;
    if (att_reader->read(reinterpret_cast<uint8_t*>(&header_), sizeof(header_)) != sizeof(header_)) {
        std::memset(&header_, 0, sizeof(header_));
        return false;
    }

    if (std::memcmp(header_.magic, ATT_MAGIC, 4) != 0 || header_.version != ATT_VERSION) {
        std::memset(&header_, 0, sizeof(header_));
        return false;
    }

    if (header_.header_size < sizeof(AttHeader)) {
        std::memset(&header_, 0, sizeof(header_));
        return false;
    }

    if (header_.tempo_point_size != sizeof(AttTempoPoint) ||
        header_.beat_marker_size != sizeof(AttBeatMarker)) {
        std::memset(&header_, 0, sizeof(header_));
        return false;
    }

    // Limit tempo curve points to a reasonable upper bound to protect against corrupt headers
    if (header_.tempo_curve_count > 65536) {
        std::memset(&header_, 0, sizeof(header_));
        return false;
    }

    // Load tempo curve points into memory (typically 1 to 50 nodes = < 400 bytes)
    tempo_points_.resize(header_.tempo_curve_count);
    if (header_.tempo_curve_count > 0) {
        if (!att_reader->seek(header_.tempo_curve_offset)) {
            tempo_points_.clear();
            std::memset(&header_, 0, sizeof(header_));
            return false;
        }
        size_t bytes = header_.tempo_curve_count * sizeof(AttTempoPoint);
        if (att_reader->read(reinterpret_cast<uint8_t*>(tempo_points_.data()), bytes) != bytes) {
            tempo_points_.clear();
            std::memset(&header_, 0, sizeof(header_));
            return false;
        }
    }

    reader_ = att_reader;
    return true;
}

const AttHeader& TempoReader::header() const { return header_; }

float TempoReader::get_bpm_at(uint32_t time_ms) const {
    if (tempo_points_.empty()) {
        return static_cast<float>(header_.global_bpm_q16) / 65536.0f;
    }

    if (time_ms <= tempo_points_.front().time_ms) {
        return static_cast<float>(tempo_points_.front().bpm_q16) / 65536.0f;
    }
    if (time_ms >= tempo_points_.back().time_ms) {
        return static_cast<float>(tempo_points_.back().bpm_q16) / 65536.0f;
    }

    // Binary search
    auto it = std::upper_bound(tempo_points_.begin(), tempo_points_.end(), time_ms,
        [](uint32_t t, const AttTempoPoint& pt) { return t < pt.time_ms; });

    size_t idx1 = std::distance(tempo_points_.begin(), it);
    size_t idx0 = idx1 - 1;

    const auto& p0 = tempo_points_[idx0];
    const auto& p1 = tempo_points_[idx1];

    uint32_t dt = p1.time_ms - p0.time_ms;
    float fraction = dt > 0 ? (static_cast<float>(time_ms - p0.time_ms) / static_cast<float>(dt)) : 0.0f;
    float bpm0 = static_cast<float>(p0.bpm_q16) / 65536.0f;
    float bpm1 = static_cast<float>(p1.bpm_q16) / 65536.0f;

    return bpm0 + fraction * (bpm1 - bpm0);
}

bool TempoReader::get_beat_at_index(uint32_t beat_index, AttBeatMarker* out_beat) const {
    if (!reader_ || !out_beat || beat_index >= header_.total_beats) return false;

    uint64_t offset = header_.beat_grid_offset + static_cast<uint64_t>(beat_index) * sizeof(AttBeatMarker);
    if (!reader_->seek(offset)) return false;

    return reader_->read(reinterpret_cast<uint8_t*>(out_beat), sizeof(AttBeatMarker)) == sizeof(AttBeatMarker);
}

bool TempoReader::get_nearest_beat(uint32_t time_ms, AttBeatMarker* out_beat) const {
    if (!reader_ || !out_beat || header_.total_beats == 0) return false;

    int low = 0;
    int high = static_cast<int>(header_.total_beats) - 1;
    AttBeatMarker best_bm{};
    bool found_any = false;
    uint32_t min_diff = 0xFFFFFFFF;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        AttBeatMarker bm{};
        if (!get_beat_at_index(static_cast<uint32_t>(mid), &bm)) break;

        uint32_t diff = (bm.time_ms > time_ms) ? (bm.time_ms - time_ms) : (time_ms - bm.time_ms);
        if (diff < min_diff) {
            min_diff = diff;
            best_bm = bm;
            found_any = true;
        }

        if (bm.time_ms == time_ms) {
            *out_beat = bm;
            return true;
        }
        if (bm.time_ms < time_ms) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    *out_beat = best_bm;
    return found_any;
}

float TempoReader::time_to_beat(uint32_t time_ms) const {
    if (header_.total_beats == 0) return 0.0f;

    AttBeatMarker b0{};
    if (!get_beat_at_index(0, &b0)) return 0.0f;
    if (time_ms <= b0.time_ms) return 0.0f;

    // Binary search for surrounding beat interval
    int low = 0;
    int high = static_cast<int>(header_.total_beats) - 1;
    int idx = 0;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        AttBeatMarker bm{};
        if (!get_beat_at_index(static_cast<uint32_t>(mid), &bm)) break;
        if (bm.time_ms <= time_ms) {
            idx = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    if (idx >= static_cast<int>(header_.total_beats) - 1) {
        return static_cast<float>(header_.total_beats - 1);
    }

    AttBeatMarker cur{};
    AttBeatMarker next{};
    if (!get_beat_at_index(static_cast<uint32_t>(idx), &cur) ||
        !get_beat_at_index(static_cast<uint32_t>(idx + 1), &next)) {
        return static_cast<float>(idx);
    }

    float span = static_cast<float>(next.time_ms - cur.time_ms);
    float frac = span > 0.0f ? (static_cast<float>(time_ms - cur.time_ms) / span) : 0.0f;
    return static_cast<float>(idx) + frac;
}

uint32_t TempoReader::beat_to_time(float beat_number) const {
    if (header_.total_beats == 0) return 0;
    if (beat_number <= 0.0f) {
        AttBeatMarker b0{};
        if (get_beat_at_index(0, &b0)) {
            return b0.time_ms;
        }
        return 0;
    }
    if (beat_number >= static_cast<float>(header_.total_beats - 1)) {
        AttBeatMarker b_last{};
        if (get_beat_at_index(header_.total_beats - 1, &b_last)) {
            return b_last.time_ms;
        }
        return 0;
    }

    uint32_t idx = static_cast<uint32_t>(beat_number);
    float frac = beat_number - static_cast<float>(idx);

    AttBeatMarker b0{};
    AttBeatMarker b1{};
    if (!get_beat_at_index(idx, &b0) || !get_beat_at_index(idx + 1, &b1)) {
        return 0;
    }

    return b0.time_ms + static_cast<uint32_t>(frac * static_cast<float>(b1.time_ms - b0.time_ms));
}

size_t TempoReader::read_beats(uint32_t start_ms, uint32_t duration_ms,
                              AttBeatMarker* out_beats, size_t max_count) const {
    if (!reader_ || !out_beats || max_count == 0 || header_.total_beats == 0) return 0;

    uint64_t end_ms = static_cast<uint64_t>(start_ms) + static_cast<uint64_t>(duration_ms);

    // Binary search lower bound for first beat with time_ms >= start_ms
    int low = 0;
    int high = static_cast<int>(header_.total_beats) - 1;
    int start_idx = static_cast<int>(header_.total_beats);
    while (low <= high) {
        int mid = low + (high - low) / 2;
        AttBeatMarker bm{};
        if (!get_beat_at_index(static_cast<uint32_t>(mid), &bm)) break;
        if (bm.time_ms >= start_ms) {
            start_idx = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    if (start_idx >= static_cast<int>(header_.total_beats)) {
        return 0;
    }

    size_t collected = 0;
    for (uint32_t i = static_cast<uint32_t>(start_idx); i < header_.total_beats && collected < max_count; ++i) {
        AttBeatMarker bm{};
        if (!get_beat_at_index(i, &bm)) break;
        if (static_cast<uint64_t>(bm.time_ms) >= end_ms) break;
        out_beats[collected++] = bm;
    }

    return collected;
}

} // namespace audio_codecs::tempo
