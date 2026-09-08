#include "audio_codecs/preview/preview_reader.h"
#include "audio_codecs/preview/preview_generator.h"
#include "audio_codecs/preview/preview_stream.h"
#include <cassert>
#include <vector>
#include <iostream>

using namespace audio_codecs::preview;

void test_stereo_overview_and_zoom() {
    constexpr size_t total_frames = 512 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 0);
    pcm[200 * 2] = 32767;      // spike on left
    pcm[200 * 2 + 1] = -32768; // trough on right

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_file(1024 * 16, 0);
    MemoryWriter writer(apv_file.data(), apv_file.size());

    PreviewGenerator generator;
    bool gen_ok = generator.init(pcm_reader, writer, nullptr, 44100, 2, true);
    assert(gen_ok);
    assert(generator.generate_all());

    // Now test PreviewReader
    MemoryReader reader(apv_file.data(), writer.size());
    PreviewReader pr;
    bool ok = pr.init(reader);
    assert(ok);
    assert(pr.sample_rate() == 44100);
    assert(pr.channels() == 2);
    assert(pr.total_frames() == total_frames);
    assert(pr.duration_ms() > 1400);
    assert(pr.header().magic == APV_MAGIC);

    // 1. Zoomed out query: read entire track into 32 points
    WaveformPointStereo overview[32]{};
    size_t pts = pr.read_preview_stereo(0, pr.duration_ms(), overview, 32);
    assert(pts == 32);
    assert(overview[0].left_max == 127);
    assert(overview[0].right_min == -128);

    // 2. Zoomed in query: read 20 ms around spike into 50 points (interpolated)
    WaveformPointStereo zoomed[50]{};
    pts = pr.read_preview_stereo(2, 10, zoomed, 50);
    assert(pts == 50);
    for (size_t i = 0; i < 50; ++i) {
        assert(zoomed[i].left_min <= zoomed[i].left_max);
        assert(zoomed[i].right_min <= zoomed[i].right_max);
    }

    // 3. Mono query on stereo file
    WaveformPointMono mono_overview[32]{};
    size_t mono_pts = pr.read_preview(0, pr.duration_ms(), mono_overview, 32);
    assert(mono_pts == 32);
    assert(mono_overview[0].max == 127);
    assert(mono_overview[0].min == -128);
}

void test_boundary_conditions() {
    constexpr size_t total_frames = 64 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 0);
    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_file(1024 * 4, 0);
    MemoryWriter writer(apv_file.data(), apv_file.size());

    PreviewGenerator generator;
    generator.init(pcm_reader, writer, nullptr, 44100, 2, true);
    generator.generate_all();

    MemoryReader reader(apv_file.data(), writer.size());
    PreviewReader pr;
    assert(pr.init(reader));

    WaveformPointStereo pts[10];

    // null out_points
    assert(pr.read_preview_stereo(0, 100, nullptr, 10) == 0);
    assert(pr.read_preview(0, 100, nullptr, 10) == 0);

    // num_points == 0
    assert(pr.read_preview_stereo(0, 100, pts, 0) == 0);

    // duration_ms == 0
    assert(pr.read_preview_stereo(0, 0, pts, 10) == 0);

    // start_ms >= duration_ms()
    assert(pr.read_preview_stereo(pr.duration_ms(), 10, pts, 10) == 0);
    assert(pr.read_preview_stereo(pr.duration_ms() + 100, 10, pts, 10) == 0);

    // uninitialized reader
    PreviewReader uninit_pr;
    assert(uninit_pr.read_preview_stereo(0, 100, pts, 10) == 0);
    assert(uninit_pr.duration_ms() == 0);
    assert(uninit_pr.sample_rate() == 0);
    assert(uninit_pr.channels() == 0);
}

void test_mono_source() {
    constexpr size_t total_frames = 128 * 128;
    std::vector<int16_t> pcm(total_frames, 0);
    pcm[50] = 30000;
    pcm[51] = -30000;

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_file(1024 * 4, 0);
    MemoryWriter writer(apv_file.data(), apv_file.size());

    PreviewGenerator generator;
    assert(generator.init(pcm_reader, writer, nullptr, 44100, 1, false));
    assert(generator.generate_all());

    MemoryReader reader(apv_file.data(), writer.size());
    PreviewReader pr;
    assert(pr.init(reader));
    assert(pr.channels() == 1);

    WaveformPointMono mono_pts[16]{};
    size_t n = pr.read_preview(0, pr.duration_ms(), mono_pts, 16);
    assert(n == 16);
    assert(mono_pts[0].max > 100);
    assert(mono_pts[0].min < -100);

    // Read stereo from mono file replicates channels
    WaveformPointStereo stereo_pts[16]{};
    n = pr.read_preview_stereo(0, pr.duration_ms(), stereo_pts, 16);
    assert(n == 16);
    assert(stereo_pts[0].left_max == stereo_pts[0].right_max);
    assert(stereo_pts[0].left_min == stereo_pts[0].right_min);
    assert(stereo_pts[0].left_max == mono_pts[0].max);
    assert(stereo_pts[0].left_min == mono_pts[0].min);
}

void test_lod_selection() {
    constexpr size_t total_frames = 512 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 0);
    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_file(1024 * 16, 0);
    MemoryWriter writer(apv_file.data(), apv_file.size());

    PreviewGenerator generator;
    generator.init(pcm_reader, writer, nullptr, 44100, 2, true);
    generator.generate_all();

    MemoryReader reader(apv_file.data(), writer.size());
    PreviewReader pr;
    assert(pr.init(reader));

    // LOD 0 chunk duration ~ 2.90 ms
    // LOD 1 chunk duration ~ 46.44 ms
    // LOD 2 chunk duration ~ 743.04 ms
    assert(pr.select_lod(1.0f) == 0);
    assert(pr.select_lod(10.0f) == 0);
    assert(pr.select_lod(47.0f) == 1);
    assert(pr.select_lod(100.0f) == 1);
    assert(pr.select_lod(750.0f) == 2);
    assert(pr.select_lod(2000.0f) == 2);
}

void test_interpolation_smoothness() {
    // Generate a file with chunk 0 = 0, chunk 1 = spike (100)
    // base chunk = 128 frames
    constexpr size_t total_frames = 64 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 0);

    // In chunk 1 (frames 128..255), put amplitude
    // 25600 / 256 = 100
    for (size_t f = 128; f < 256; ++f) {
        pcm[f * 2] = 25600;
        pcm[f * 2 + 1] = -25600;
    }

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_file(1024 * 4, 0);
    MemoryWriter writer(apv_file.data(), apv_file.size());

    PreviewGenerator generator;
    assert(generator.init(pcm_reader, writer, nullptr, 44100, 2, true));
    assert(generator.generate_all());

    MemoryReader reader(apv_file.data(), writer.size());
    PreviewReader pr;
    assert(pr.init(reader));

    // Chunk 0 is at 0 ms, Chunk 1 is at ~2.90 ms
    // Zoom in from 0 ms to 2 ms with 20 points
    WaveformPointStereo pts[20]{};
    size_t n = pr.read_preview_stereo(0, 2, pts, 20);
    assert(n == 20);

    // Values should start near 0 and monotonically increase towards ~70
    for (size_t i = 1; i < 20; ++i) {
        assert(pts[i].left_max >= pts[i - 1].left_max);
        assert(pts[i].right_min <= pts[i - 1].right_min);
    }
}

void test_invalid_headers() {
    WaveformPointStereo dummy[4];

    // 1. Buffer too small
    uint8_t tiny_buf[64]{0};
    MemoryReader r1(tiny_buf, sizeof(tiny_buf));
    PreviewReader pr1;
    assert(!pr1.init(r1));
    assert(pr1.read_preview_stereo(0, 10, dummy, 4) == 0);

    // 2. Bad magic
    ApvHeader bad_hdr{};
    bad_hdr.magic = 0x12345678;
    bad_hdr.version = APV_VERSION;
    MemoryReader r2(reinterpret_cast<uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    PreviewReader pr2;
    assert(!pr2.init(r2));
    assert(pr2.read_preview_stereo(0, 10, dummy, 4) == 0);

    // 3. Bad version
    bad_hdr.magic = APV_MAGIC;
    bad_hdr.version = 99;
    MemoryReader r3(reinterpret_cast<uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    PreviewReader pr3;
    assert(!pr3.init(r3));
    assert(pr3.read_preview_stereo(0, 10, dummy, 4) == 0);

    // 4. Bad channels
    bad_hdr.version = APV_VERSION;
    bad_hdr.channels = 5;
    MemoryReader r4(reinterpret_cast<uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    PreviewReader pr4;
    assert(!pr4.init(r4));
    assert(pr4.read_preview_stereo(0, 10, dummy, 4) == 0);
}

void test_dc_offset_decimation() {
    // Construct an APV file where chunks have purely positive min/max: min = 20, max = 50
    ApvHeader hdr{};
    hdr.magic = APV_MAGIC;
    hdr.version = APV_VERSION;
    hdr.channels = 2;
    hdr.bytes_per_chunk = 4;
    hdr.sample_rate = 44100;
    hdr.samples_per_base_chunk = 128;
    hdr.total_pcm_frames = 64 * 128;
    hdr.duration_ms = (64 * 128 * 1000) / 44100;
    hdr.lod_count = 1;
    hdr.lods[0].downsample_ratio = 1;
    hdr.lods[0].chunk_count = 64;
    hdr.lods[0].file_offset = sizeof(ApvHeader);

    std::vector<uint8_t> apv_buf(sizeof(ApvHeader) + 64 * sizeof(WaveformPointStereo));
    std::memcpy(apv_buf.data(), &hdr, sizeof(hdr));

    WaveformPointStereo* chunks = reinterpret_cast<WaveformPointStereo*>(apv_buf.data() + sizeof(ApvHeader));
    for (size_t c = 0; c < 64; ++c) {
        chunks[c] = {20, 50, 25, 45};
    }

    MemoryReader reader(apv_buf.data(), apv_buf.size());
    PreviewReader pr;
    assert(pr.init(reader));

    // Decimate to 8 points (each point spans 8 chunks)
    WaveformPointStereo stereo_pts[8]{};
    size_t n = pr.read_preview_stereo(0, pr.duration_ms(), stereo_pts, 8);
    assert(n == 8);
    for (size_t i = 0; i < 8; ++i) {
        // min should be 20/25, not 0!
        assert(stereo_pts[i].left_min == 20);
        assert(stereo_pts[i].left_max == 50);
        assert(stereo_pts[i].right_min == 25);
        assert(stereo_pts[i].right_max == 45);
    }

    WaveformPointMono mono_pts[8]{};
    n = pr.read_preview(0, pr.duration_ms(), mono_pts, 8);
    assert(n == 8);
    for (size_t i = 0; i < 8; ++i) {
        assert(mono_pts[i].min == 20);
        assert(mono_pts[i].max == 50);
    }
}

int main() {
    test_stereo_overview_and_zoom();
    test_boundary_conditions();
    test_mono_source();
    test_lod_selection();
    test_interpolation_smoothness();
    test_invalid_headers();
    test_dc_offset_decimation();

    std::cout << "test_preview_reader PASSED\n";
    return 0;
}
