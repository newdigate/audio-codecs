#include <audio_codecs/tempo/tempo_reader.h>
#include <audio_codecs/tempo/tempo_generator.h>
#include <audio_codecs/spectrum/spectrum_types.h>
#include <audio_codecs/preview/preview_stream.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

using namespace audio_codecs::tempo;
using namespace audio_codecs::preview;

void test_tempo_reader_queries() {
    std::vector<uint8_t> buffer(4096, 0);

    AttHeader hdr{};
    std::memcpy(hdr.magic, ATT_MAGIC, 4);
    hdr.version = ATT_VERSION;
    hdr.header_size = 128;
    hdr.duration_ms = 10000;
    hdr.global_bpm_q16 = (120 << 16);
    hdr.time_signature_num = 4;
    hdr.time_signature_denom = 4;
    hdr.total_beats = 20;
    hdr.tempo_curve_offset = 128;
    hdr.tempo_curve_count = 2;
    hdr.tempo_point_size = sizeof(AttTempoPoint);
    hdr.beat_grid_offset = 128 + 2 * sizeof(AttTempoPoint);
    hdr.beat_grid_count = 20;
    hdr.beat_marker_size = sizeof(AttBeatMarker);

    std::memcpy(buffer.data(), &hdr, sizeof(hdr));

    AttTempoPoint points[2] = {
        {0, 120 << 16},
        {10000, 140 << 16}
    };
    std::memcpy(buffer.data() + 128, points, sizeof(points));

    std::vector<AttBeatMarker> beats(20);
    for (uint32_t i = 0; i < 20; ++i) {
        beats[i].time_ms = i * 500;
        beats[i].bar_index = (i / 4) + 1;
        beats[i].beat_within_bar = (i % 4) + 1;
        beats[i].flags = (i % 4 == 0) ? ATT_BEAT_FLAG_DOWNBEAT : 0;
        beats[i].local_bpm_q16 = (120 << 16);
    }
    std::memcpy(buffer.data() + 128 + sizeof(points), beats.data(), sizeof(AttBeatMarker) * 20);

    MemoryReader reader(buffer.data(), buffer.size());
    TempoReader tempo_reader;
    bool ok = tempo_reader.init(&reader);
    assert(ok);

    // Header access
    const AttHeader& h = tempo_reader.header();
    assert(std::memcmp(h.magic, ATT_MAGIC, 4) == 0);
    assert(h.total_beats == 20);

    // Linear BPM interpolation at 5000 ms -> halfway between 120 and 140 -> 130 BPM
    float bpm_mid = tempo_reader.get_bpm_at(5000);
    assert(std::fabs(bpm_mid - 130.0f) < 0.1f);

    // BPM clamping before start and after end
    float bpm_start = tempo_reader.get_bpm_at(0);
    assert(std::fabs(bpm_start - 120.0f) < 0.1f);
    float bpm_end = tempo_reader.get_bpm_at(15000);
    assert(std::fabs(bpm_end - 140.0f) < 0.1f);

    // Nearest beat queries
    AttBeatMarker bm{};
    bool found = tempo_reader.get_nearest_beat(1490, &bm);
    assert(found);
    assert(bm.time_ms == 1500);
    assert(bm.beat_within_bar == 4);

    found = tempo_reader.get_nearest_beat(0, &bm);
    assert(found);
    assert(bm.time_ms == 0);
    assert(bm.beat_within_bar == 1);

    // Exact beat query by index
    found = tempo_reader.get_beat_at_index(3, &bm);
    assert(found);
    assert(bm.time_ms == 1500);

    bool out_of_bounds = tempo_reader.get_beat_at_index(20, &bm);
    assert(!out_of_bounds);

    // Musical time conversions
    float beat_num = tempo_reader.time_to_beat(1500);
    assert(std::fabs(beat_num - 3.0f) < 0.05f);

    beat_num = tempo_reader.time_to_beat(1750);
    assert(std::fabs(beat_num - 3.5f) < 0.05f);

    uint32_t t_ms = tempo_reader.beat_to_time(3.0f);
    assert(t_ms == 1500);

    t_ms = tempo_reader.beat_to_time(3.5f);
    assert(t_ms == 1750);

    // Beat clamping
    beat_num = tempo_reader.time_to_beat(0);
    assert(std::fabs(beat_num - 0.0f) < 0.001f);
    t_ms = tempo_reader.beat_to_time(0.0f);
    assert(t_ms == 0);
    t_ms = tempo_reader.beat_to_time(-5.0f);
    assert(t_ms == 0);
    t_ms = tempo_reader.beat_to_time(100.0f);
    assert(t_ms == 9500);

    // Windowed reading
    AttBeatMarker slice[10];
    size_t count = tempo_reader.read_beats(1000, 2000, slice, 10);
    assert(count == 4); // 1000, 1500, 2000, 2500
    assert(slice[0].time_ms == 1000);
    assert(slice[1].time_ms == 1500);
    assert(slice[2].time_ms == 2000);
    assert(slice[3].time_ms == 2500);

    // Windowed reading max_count limit
    count = tempo_reader.read_beats(1000, 2000, slice, 2);
    assert(count == 2);
    assert(slice[0].time_ms == 1000);
    assert(slice[1].time_ms == 1500);

    // Windowed reading beyond end
    count = tempo_reader.read_beats(20000, 1000, slice, 10);
    assert(count == 0);
}

void test_tempo_reader_invalid_inputs() {
    TempoReader reader;
    // Null reader
    bool ok = reader.init(nullptr);
    assert(!ok);

    // Truncated header
    uint8_t small_buf[64] = {0};
    MemoryReader small_reader(small_buf, sizeof(small_buf));
    ok = reader.init(&small_reader);
    assert(!ok);

    // Bad magic
    AttHeader bad_hdr{};
    std::memcpy(bad_hdr.magic, "XXXX", 4);
    bad_hdr.version = ATT_VERSION;
    bad_hdr.header_size = 128;
    MemoryReader bad_magic_reader(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    ok = reader.init(&bad_magic_reader);
    assert(!ok);

    // Bad version
    bad_hdr = {};
    std::memcpy(bad_hdr.magic, ATT_MAGIC, 4);
    bad_hdr.version = 999;
    bad_hdr.header_size = 128;
    MemoryReader bad_ver_reader(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    ok = reader.init(&bad_ver_reader);
    assert(!ok);

    // Bad tempo_point_size
    bad_hdr = {};
    std::memcpy(bad_hdr.magic, ATT_MAGIC, 4);
    bad_hdr.version = ATT_VERSION;
    bad_hdr.header_size = 128;
    bad_hdr.tempo_point_size = 4; // should be sizeof(AttTempoPoint) = 8
    bad_hdr.beat_marker_size = sizeof(AttBeatMarker);
    MemoryReader bad_tps_reader(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    ok = reader.init(&bad_tps_reader);
    assert(!ok);

    // Bad beat_marker_size
    bad_hdr = {};
    std::memcpy(bad_hdr.magic, ATT_MAGIC, 4);
    bad_hdr.version = ATT_VERSION;
    bad_hdr.header_size = 128;
    bad_hdr.tempo_point_size = sizeof(AttTempoPoint);
    bad_hdr.beat_marker_size = 4; // should be sizeof(AttBeatMarker) = 16
    MemoryReader bad_bms_reader(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    ok = reader.init(&bad_bms_reader);
    assert(!ok);

    // Query on uninitialized reader
    TempoReader uninit;
    AttBeatMarker bm{};
    assert(!uninit.get_beat_at_index(0, &bm));
    assert(!uninit.get_nearest_beat(1000, &bm));
    assert(uninit.read_beats(0, 1000, &bm, 1) == 0);
    assert(uninit.get_bpm_at(0) == 0.0f);
    assert(uninit.time_to_beat(1000) == 0.0f);
    assert(uninit.beat_to_time(1.0f) == 0);
}

void test_tempo_reader_generated_file_roundtrip() {
    // Generate an ATT file using TempoGenerator and read back using TempoReader
    const uint32_t total_frames = 2580;
    const size_t asv_size = sizeof(audio_codecs::spectrum::AsvHeader) + total_frames * 64;
    std::vector<uint8_t> asv_buffer(asv_size, 0);

    audio_codecs::spectrum::AsvHeader hdr{};
    hdr.magic = audio_codecs::spectrum::ASV_MAGIC;
    hdr.version = audio_codecs::spectrum::ASV_VERSION;
    hdr.duration_ms = 30000;
    hdr.sample_rate = 44100;
    hdr.num_bands = 64;
    hdr.hop_size = 512;
    hdr.fft_size = 1024;
    hdr.lod_count = 1;
    hdr.lods[0].file_offset = 128;
    hdr.lods[0].frame_count = total_frames;
    hdr.lods[0].downsample_ratio = 1;
    std::memcpy(asv_buffer.data(), &hdr, sizeof(hdr));

    for (uint32_t f = 0; f < total_frames; ++f) {
        uint8_t* frame_ptr = asv_buffer.data() + 128 + f * 64;
        if (f % 43 == 0) {
            int beat = (f / 43) % 4;
            if (beat == 0 || beat == 2) {
                for (int b = 2; b <= 8; ++b) frame_ptr[b] = 220;
            } else {
                for (int b = 20; b <= 32; ++b) frame_ptr[b] = 200;
            }
        }
    }

    MemoryReader asv_reader(asv_buffer.data(), asv_buffer.size());
    std::vector<uint8_t> att_buffer(32768, 0);
    MemoryWriter att_writer(att_buffer.data(), att_buffer.size());

    TempoGenerator generator;
    bool gen_ok = generator.init(&asv_reader, &att_writer);
    assert(gen_ok);
    while (generator.step(128)) {}
    assert(generator.is_complete());

    MemoryReader att_reader(att_buffer.data(), att_writer.size());
    TempoReader tempo_reader;
    bool read_ok = tempo_reader.init(&att_reader);
    assert(read_ok);

    const AttHeader& att_hdr = tempo_reader.header();
    assert(std::memcmp(att_hdr.magic, ATT_MAGIC, 4) == 0);
    assert(att_hdr.total_beats > 50);

    float read_bpm = tempo_reader.get_bpm_at(10000);
    assert(read_bpm >= 119.0f && read_bpm <= 121.0f);

    AttBeatMarker first_beat{};
    bool found_first = tempo_reader.get_nearest_beat(0, &first_beat);
    assert(found_first);
    assert(first_beat.time_ms > 0);

    AttBeatMarker window[4];
    size_t count = tempo_reader.read_beats(first_beat.time_ms, 2000, window, 4);
    assert(count > 0 && count <= 4);
}

int main() {
    test_tempo_reader_queries();
    test_tempo_reader_invalid_inputs();
    test_tempo_reader_generated_file_roundtrip();
    std::cout << "test_tempo_reader PASSED\n";
    return 0;
}
