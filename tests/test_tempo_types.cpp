#include <audio_codecs/tempo/tempo_types.h>
#include <audio_codecs/preview/preview_stream.h>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace audio_codecs::tempo;
using namespace audio_codecs::preview;

void test_sizes_and_alignments() {
    static_assert(sizeof(AttHeader) == 128, "AttHeader size must be 128 bytes");
    static_assert(sizeof(AttTempoPoint) == 8, "AttTempoPoint size must be 8 bytes");
    static_assert(sizeof(AttBeatMarker) == 16, "AttBeatMarker size must be 16 bytes");

    size_t s_hdr = sizeof(AttHeader);
    size_t s_pt = sizeof(AttTempoPoint);
    size_t s_bm = sizeof(AttBeatMarker);

    assert(s_hdr == 128);
    assert(s_pt == 8);
    assert(s_bm == 16);
}

void test_header_defaults() {
    AttHeader hdr{};
    hdr.magic[0] = 'A';
    hdr.magic[1] = 'T';
    hdr.magic[2] = 'T';
    hdr.magic[3] = '1';
    hdr.version = ATT_VERSION;
    hdr.header_size = 128;
    hdr.duration_ms = 180000;
    hdr.sample_rate = 44100;
    hdr.global_bpm_q16 = (120 << 16);
    hdr.confidence = 240;
    hdr.time_signature_num = 4;
    hdr.time_signature_denom = 4;
    hdr.flags = ATT_FLAG_CONSTANT_TEMPO | ATT_FLAG_HAS_DOWNBEATS;
    hdr.first_beat_ms = 500;
    hdr.first_downbeat_ms = 500;
    hdr.total_beats = 360;
    hdr.total_bars = 90;
    hdr.tempo_curve_offset = 128;
    hdr.tempo_curve_count = 1;
    hdr.tempo_point_size = sizeof(AttTempoPoint);
    hdr.beat_grid_offset = 128 + sizeof(AttTempoPoint);
    hdr.beat_grid_count = 360;
    hdr.beat_marker_size = sizeof(AttBeatMarker);

    uint8_t buffer[128];
    MemoryWriter writer(buffer, sizeof(buffer));
    size_t written = writer.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    assert(written == 128);

    AttHeader read_hdr{};
    MemoryReader reader(buffer, sizeof(buffer));
    size_t read_bytes = reader.read(reinterpret_cast<uint8_t*>(&read_hdr), sizeof(read_hdr));
    assert(read_bytes == 128);
    assert(std::memcmp(&hdr, &read_hdr, sizeof(AttHeader)) == 0);

    int cmp = std::memcmp(read_hdr.magic, "ATT1", 4);
    assert(cmp == 0);
    assert(read_hdr.version == ATT_VERSION);
    assert(read_hdr.duration_ms == 180000);
    assert(read_hdr.global_bpm_q16 == (120 << 16));
    assert(read_hdr.total_beats == 360);
    assert(read_hdr.total_bars == 90);
}

int main() {
    test_sizes_and_alignments();
    test_header_defaults();
    std::cout << "test_tempo_types PASSED\n";
    return 0;
}
