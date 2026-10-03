#include <audio_codecs/tempo/tempo_generator.h>
#include <audio_codecs/spectrum/spectrum_types.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::tempo {

TempoGenerator::TempoGenerator()
    : asv_reader_(nullptr),
      att_writer_(nullptr),
      is_complete_(false),
      has_error_(false),
      progress_(0.0f),
      total_frames_(0),
      current_frame_(0),
      hop_size_(512),
      sample_rate_(44100),
      frame_rate_(86.133f),
      last_beat_frame_(0),
      current_period_frames_(43.0f),
      has_first_beat_(false),
      beat_count_(0) {
    std::memset(&header_, 0, sizeof(header_));
    std::memset(metric_scores_, 0, sizeof(metric_scores_));
}

TempoGenerator::~TempoGenerator() = default;

bool TempoGenerator::init(preview::SeekableReader* asv_reader,
                          preview::SeekableWriter* att_writer,
                          const TempoConfig& config) {
    if (!asv_reader || !att_writer) {
        has_error_ = true;
        return false;
    }

    asv_reader_ = asv_reader;
    att_writer_ = att_writer;
    config_ = config;
    if (config_.time_signature_num == 0) {
        config_.time_signature_num = 4;
    }
    if (config_.time_signature_denom == 0) {
        config_.time_signature_denom = 4;
    }

    // Read ASV1 header
    spectrum::AsvHeader asv_hdr{};
    if (asv_reader_->read(reinterpret_cast<uint8_t*>(&asv_hdr), sizeof(asv_hdr)) != sizeof(asv_hdr)) {
        has_error_ = true;
        return false;
    }

    if (asv_hdr.magic != spectrum::ASV_MAGIC || asv_hdr.version != spectrum::ASV_VERSION ||
        asv_hdr.sample_rate == 0 || asv_hdr.lod_count == 0) {
        has_error_ = true;
        return false;
    }

    total_frames_ = asv_hdr.lods[0].frame_count;
    sample_rate_ = asv_hdr.sample_rate;
    hop_size_ = asv_hdr.hop_size > 0 ? asv_hdr.hop_size : 512;
    frame_rate_ = static_cast<float>(sample_rate_) / static_cast<float>(hop_size_);

    novelty_extractor_.reset();
    inducer_.reset(frame_rate_);

    current_frame_ = 0;
    progress_ = 0.0f;
    is_complete_ = false;
    has_error_ = false;
    has_first_beat_ = false;
    last_beat_frame_ = 0;
    current_period_frames_ = (60.0f * frame_rate_) / 120.0f;
    beat_count_ = 0;
    tempo_points_.clear();
    beat_markers_.clear();

    // Pre-reserve vectors to guarantee zero heap allocations during steady-state step()
    if (total_frames_ > 0) {
        tempo_points_.reserve((total_frames_ / 64) + 64);
        beat_markers_.reserve((total_frames_ / 10) + 128);
    }

    std::memset(metric_scores_, 0, sizeof(metric_scores_));

    std::memset(&header_, 0, sizeof(header_));
    std::memcpy(header_.magic, ATT_MAGIC, 4);
    header_.version = ATT_VERSION;
    header_.header_size = ATT_HEADER_SIZE;
    header_.duration_ms = asv_hdr.duration_ms;
    header_.sample_rate = sample_rate_;
    header_.time_signature_num = config_.time_signature_num;
    header_.time_signature_denom = config_.time_signature_denom;
    header_.tempo_point_size = sizeof(AttTempoPoint);
    header_.beat_marker_size = sizeof(AttBeatMarker);

    // Seek to LOD 0 start
    uint64_t lod0_offset = asv_hdr.lods[0].file_offset > 0 ? asv_hdr.lods[0].file_offset : 128;
    asv_reader_->seek(lod0_offset);

    return true;
}

bool TempoGenerator::step(size_t max_frames) {
    if (is_complete_ || has_error_) return false;
    if (max_frames == 0 && current_frame_ < total_frames_) return true;

    size_t frames_to_process = std::min(static_cast<size_t>(total_frames_ - current_frame_), max_frames);
    if (frames_to_process == 0) {
        finalize_att_file();
        return false;
    }

    uint8_t bands[64];
    for (size_t i = 0; i < frames_to_process; ++i) {
        if (asv_reader_->read(bands, 64) != 64) {
            has_error_ = true;
            return false;
        }

        NoveltySample nov = novelty_extractor_.process_frame(bands);
        inducer_.feed_sample(nov.novelty);

        // Update tempo estimate every 64 frames once sufficient frames exist
        if (current_frame_ >= 256 && (current_frame_ % 64 == 0)) {
            TempoEstimate est = inducer_.estimate_tempo();
            if (est.confidence > 50) {
                current_period_frames_ = est.lag_frames;
                uint32_t bpm_q16 = static_cast<uint32_t>(est.bpm * 65536.0f);
                if (tempo_points_.empty()) {
                    tempo_points_.push_back({0, bpm_q16});
                } else {
                    float last_bpm = static_cast<float>(tempo_points_.back().bpm_q16) / 65536.0f;
                    if (std::fabs(est.bpm - last_bpm) >= config_.tempo_drift_threshold_bpm) {
                        uint32_t time_ms = static_cast<uint32_t>(static_cast<float>(current_frame_) * (1000.0f / frame_rate_));
                        tempo_points_.push_back({time_ms, bpm_q16});
                    }
                }
            }
        }

        // Beat tracking
        if (!has_first_beat_) {
            if (nov.novelty > 30.0f) {
                has_first_beat_ = true;
                last_beat_frame_ = current_frame_;
                uint32_t time_ms = static_cast<uint32_t>(static_cast<float>(current_frame_) * (1000.0f / frame_rate_));
                AttBeatMarker bm{};
                bm.time_ms = time_ms;
                bm.bar_index = 1;
                bm.beat_within_bar = 1;
                bm.flags = 0;
                bm.local_bpm_q16 = static_cast<uint32_t>((60.0f * frame_rate_ / current_period_frames_) * 65536.0f);
                beat_markers_.push_back(bm);
                metric_scores_[0] += (nov.bass_flux - nov.snare_flux);
                beat_count_ = 1;
            }
        } else {
            float frames_since_beat = static_cast<float>(current_frame_ - last_beat_frame_);
            if (frames_since_beat >= 0.85f * current_period_frames_) {
                if (nov.novelty > 20.0f || frames_since_beat >= 1.15f * current_period_frames_) {
                    last_beat_frame_ = current_frame_;
                    uint32_t time_ms = static_cast<uint32_t>(static_cast<float>(current_frame_) * (1000.0f / frame_rate_));
                    AttBeatMarker bm{};
                    bm.time_ms = time_ms;
                    bm.bar_index = (beat_count_ / config_.time_signature_num) + 1;
                    bm.beat_within_bar = (beat_count_ % config_.time_signature_num) + 1;
                    bm.flags = (nov.novelty <= 20.0f) ? ATT_BEAT_FLAG_INTERPOLATED : 0;
                    bm.local_bpm_q16 = static_cast<uint32_t>((60.0f * frame_rate_ / current_period_frames_) * 65536.0f);
                    beat_markers_.push_back(bm);

                    // Track metric kick vs snare score across candidate phases
                    int phase = beat_count_ % 4;
                    metric_scores_[phase] += (nov.bass_flux - nov.snare_flux);
                    beat_count_++;
                }
            }
        }

        current_frame_++;
    }

    progress_ = total_frames_ > 0 ? (static_cast<float>(current_frame_) / static_cast<float>(total_frames_)) : 1.0f;
    return true;
}

bool TempoGenerator::finalize_att_file() {
    if (tempo_points_.empty()) {
        TempoEstimate est = inducer_.estimate_tempo();
        tempo_points_.push_back({0, static_cast<uint32_t>(est.bpm * 65536.0f)});
    }

    // Determine downbeat phase offset
    int best_phase = 0;
    float best_phase_score = -1e9f;
    for (int p = 0; p < 4; ++p) {
        if (metric_scores_[p] > best_phase_score) {
            best_phase_score = metric_scores_[p];
            best_phase = p;
        }
    }

    // Realign beat and bar numbers based on detected downbeat phase
    bool first_downbeat_set = false;
    for (size_t i = 0; i < beat_markers_.size(); ++i) {
        int adjusted_beat = static_cast<int>(i) - best_phase;
        if (adjusted_beat >= 0) {
            beat_markers_[i].bar_index = (adjusted_beat / config_.time_signature_num) + 1;
            beat_markers_[i].beat_within_bar = (adjusted_beat % config_.time_signature_num) + 1;
            if (beat_markers_[i].beat_within_bar == 1) {
                beat_markers_[i].flags |= ATT_BEAT_FLAG_DOWNBEAT;
                if (!first_downbeat_set) {
                    header_.first_downbeat_ms = beat_markers_[i].time_ms;
                    first_downbeat_set = true;
                }
            }
        } else {
            beat_markers_[i].bar_index = 0;
            beat_markers_[i].beat_within_bar = static_cast<uint16_t>(config_.time_signature_num + adjusted_beat + 1);
        }
    }

    if (!beat_markers_.empty()) {
        header_.first_beat_ms = beat_markers_.front().time_ms;
        header_.total_beats = static_cast<uint32_t>(beat_markers_.size());
        header_.total_bars = beat_markers_.back().bar_index;
    }

    TempoEstimate final_est = inducer_.estimate_tempo();
    header_.global_bpm_q16 = static_cast<uint32_t>(final_est.bpm * 65536.0f);
    header_.confidence = final_est.confidence;
    header_.flags = 0;
    if (first_downbeat_set) {
        header_.flags |= ATT_FLAG_HAS_DOWNBEATS;
    }
    if (tempo_points_.size() <= 2) {
        header_.flags |= ATT_FLAG_CONSTANT_TEMPO;
    }

    header_.tempo_curve_offset = sizeof(AttHeader);
    header_.tempo_curve_count = static_cast<uint32_t>(tempo_points_.size());
    header_.tempo_point_size = sizeof(AttTempoPoint);
    header_.beat_grid_offset = header_.tempo_curve_offset + tempo_points_.size() * sizeof(AttTempoPoint);
    header_.beat_grid_count = static_cast<uint32_t>(beat_markers_.size());
    header_.beat_marker_size = sizeof(AttBeatMarker);

    // Write header
    att_writer_->seek(0);
    if (att_writer_->write(reinterpret_cast<const uint8_t*>(&header_), sizeof(header_)) != sizeof(header_)) {
        has_error_ = true;
        return false;
    }

    // Write tempo curve points
    if (!tempo_points_.empty()) {
        size_t bytes = tempo_points_.size() * sizeof(AttTempoPoint);
        if (att_writer_->write(reinterpret_cast<const uint8_t*>(tempo_points_.data()), bytes) != bytes) {
            has_error_ = true;
            return false;
        }
    }

    // Write beat markers
    if (!beat_markers_.empty()) {
        size_t bytes = beat_markers_.size() * sizeof(AttBeatMarker);
        if (att_writer_->write(reinterpret_cast<const uint8_t*>(beat_markers_.data()), bytes) != bytes) {
            has_error_ = true;
            return false;
        }
    }

    att_writer_->flush();

    progress_ = 1.0f;
    is_complete_ = true;
    return true;
}

float TempoGenerator::progress() const { return progress_; }
bool TempoGenerator::is_complete() const { return is_complete_; }
bool TempoGenerator::has_error() const { return has_error_; }
const AttHeader& TempoGenerator::header() const { return header_; }

} // namespace audio_codecs::tempo
