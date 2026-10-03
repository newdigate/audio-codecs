#pragma once

#include <audio_codecs/tempo/tempo_types.h>
#include <audio_codecs/tempo/tempo_novelty.h>
#include <audio_codecs/tempo/tempo_inducer.h>
#include <audio_codecs/preview/preview_stream.h>
#include <vector>

namespace audio_codecs::tempo {

struct TempoConfig {
    float    min_bpm                   = 60.0f;
    float    max_bpm                   = 200.0f;
    uint8_t  time_signature_num        = 4;
    uint8_t  time_signature_denom      = 4;
    float    tempo_drift_threshold_bpm = 0.25f;
};

class TempoGenerator {
public:
    TempoGenerator();
    ~TempoGenerator();

    bool init(preview::SeekableReader* asv_reader,
              preview::SeekableWriter* att_writer,
              const TempoConfig& config = {});

    bool step(size_t max_frames = 128);

    float progress() const;
    bool is_complete() const;
    bool has_error() const;
    const AttHeader& header() const;

private:
    preview::SeekableReader* asv_reader_;
    preview::SeekableWriter* att_writer_;
    TempoConfig config_;

    AttHeader header_;
    bool is_complete_;
    bool has_error_;
    float progress_;

    uint32_t total_frames_;
    uint32_t current_frame_;
    uint32_t hop_size_;
    uint32_t sample_rate_;
    float frame_rate_;

    TempoNoveltyExtractor novelty_extractor_;
    TempoInducer inducer_;

    // Beat tracking internal state
    uint32_t last_beat_frame_;
    float current_period_frames_;
    bool has_first_beat_;

    // In-memory queues for writing output chunks
    std::vector<AttTempoPoint> tempo_points_;
    std::vector<AttBeatMarker> beat_markers_;

    // Metric phase accumulation
    float metric_scores_[4];
    uint32_t beat_count_;

    bool finalize_att_file();
};

} // namespace audio_codecs::tempo
