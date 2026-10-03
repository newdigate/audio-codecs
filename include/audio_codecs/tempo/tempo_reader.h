#pragma once

#include <audio_codecs/tempo/tempo_types.h>
#include <audio_codecs/preview/preview_stream.h>
#include <vector>

namespace audio_codecs::tempo {

class TempoReader {
public:
    TempoReader();
    ~TempoReader();

    bool init(preview::SeekableReader* att_reader);
    const AttHeader& header() const;

    float get_bpm_at(uint32_t time_ms) const;

    bool get_beat_at_index(uint32_t beat_index, AttBeatMarker* out_beat) const;
    bool get_nearest_beat(uint32_t time_ms, AttBeatMarker* out_beat) const;

    float time_to_beat(uint32_t time_ms) const;
    uint32_t beat_to_time(float beat_number) const;

    size_t read_beats(uint32_t start_ms, uint32_t duration_ms,
                      AttBeatMarker* out_beats, size_t max_count) const;

private:
    preview::SeekableReader* reader_;
    AttHeader header_;
    std::vector<AttTempoPoint> tempo_points_;
};

} // namespace audio_codecs::tempo
