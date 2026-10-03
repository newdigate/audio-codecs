#include <audio_codecs/tempo/tempo_generator.h>
#include <audio_codecs/spectrum/spectrum_types.h>
#include <audio_codecs/preview/preview_stream.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace audio_codecs::tempo;
using namespace audio_codecs::spectrum;
using namespace audio_codecs::preview;

void test_tempo_generator_synthetic_120bpm() {
    // Construct an in-memory ASV1 file
    // 30 seconds of audio at 44100 Hz -> 30000 ms -> ~2583 frames
    const uint32_t total_frames = 2580;
    const size_t asv_size = sizeof(AsvHeader) + total_frames * 64;
    std::vector<uint8_t> asv_buffer(asv_size, 0);

    AsvHeader hdr{};
    hdr.magic = ASV_MAGIC;
    hdr.version = ASV_VERSION;
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

    // Place kick drum on beat 1 & 3, snare on beat 2 & 4 at 120 BPM
    // Beat interval = 43 frames
    for (uint32_t f = 0; f < total_frames; ++f) {
        uint8_t* frame_ptr = asv_buffer.data() + 128 + f * 64;
        if (f % 43 == 0) {
            int beat = (f / 43) % 4;
            if (beat == 0 || beat == 2) {
                // Kick drum (bands 2..8)
                for (int b = 2; b <= 8; ++b) frame_ptr[b] = 220;
            } else {
                // Snare drum (bands 20..32)
                for (int b = 20; b <= 32; ++b) frame_ptr[b] = 200;
            }
        }
    }

    MemoryReader asv_reader(asv_buffer.data(), asv_buffer.size());
    std::vector<uint8_t> att_buffer(32768, 0);
    MemoryWriter att_writer(att_buffer.data(), att_buffer.size());

    TempoGenerator generator;
    bool init_ok = generator.init(&asv_reader, &att_writer);
    assert(init_ok);

    while (generator.step(128)) {}

    bool complete = generator.is_complete();
    bool has_err = generator.has_error();
    assert(complete);
    assert(!has_err);
    assert(generator.progress() == 1.0f);

    const AttHeader& out_hdr = generator.header();
    assert(std::memcmp(out_hdr.magic, "ATT1", 4) == 0);
    float detected_bpm = static_cast<float>(out_hdr.global_bpm_q16) / 65536.0f;
    assert(detected_bpm >= 119.0f && detected_bpm <= 121.0f);
    assert(out_hdr.total_beats > 50);
    assert(out_hdr.total_bars > 12);
    assert(out_hdr.first_downbeat_ms > 0);
    assert((out_hdr.flags & ATT_FLAG_HAS_DOWNBEATS) != 0);

    // Verify written ATT1 stream
    MemoryReader written_reader(att_buffer.data(), att_writer.size());
    AttHeader read_hdr{};
    size_t hdr_bytes = written_reader.read(reinterpret_cast<uint8_t*>(&read_hdr), sizeof(read_hdr));
    assert(hdr_bytes == sizeof(read_hdr));
    assert(std::memcmp(read_hdr.magic, "ATT1", 4) == 0);
    assert(read_hdr.total_beats == out_hdr.total_beats);
    assert(read_hdr.total_bars == out_hdr.total_bars);
    assert(read_hdr.beat_grid_count == out_hdr.beat_grid_count);

    // Read beat markers and verify downbeat flags
    bool seek_ok = written_reader.seek(read_hdr.beat_grid_offset);
    assert(seek_ok);
    std::vector<AttBeatMarker> read_markers(read_hdr.beat_grid_count);
    size_t markers_bytes = written_reader.read(
        reinterpret_cast<uint8_t*>(read_markers.data()),
        read_markers.size() * sizeof(AttBeatMarker));
    assert(markers_bytes == read_markers.size() * sizeof(AttBeatMarker));

    size_t downbeat_count = 0;
    for (const auto& marker : read_markers) {
        if (marker.beat_within_bar == 1) {
            assert((marker.flags & ATT_BEAT_FLAG_DOWNBEAT) != 0);
            downbeat_count++;
        }
    }
    assert(downbeat_count > 10);
}

void test_tempo_generator_invalid_inputs() {
    TempoGenerator gen;
    // Null pointers
    bool ok1 = gen.init(nullptr, nullptr);
    assert(!ok1);
    assert(gen.has_error());

    // Bad magic
    std::vector<uint8_t> bad_asv(256, 0);
    MemoryReader bad_reader(bad_asv.data(), bad_asv.size());
    std::vector<uint8_t> att_buf(1024, 0);
    MemoryWriter att_writer(att_buf.data(), att_buf.size());

    TempoGenerator gen2;
    bool ok2 = gen2.init(&bad_reader, &att_writer);
    assert(!ok2);
    assert(gen2.has_error());
}

int main() {
    test_tempo_generator_synthetic_120bpm();
    test_tempo_generator_invalid_inputs();
    std::cout << "test_tempo_generator PASSED\n";
    return 0;
}
