#include "audio_codecs/spectrum/spectrum_reader.h"
#include "audio_codecs/spectrum/spectrum_generator.h"
#include "audio_codecs/spectrum/desktop_fft.h"
#include "audio_codecs/preview/preview_stream.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cstring>

using namespace audio_codecs::spectrum;
using namespace audio_codecs::preview;

void test_basic_reading() {
    DesktopRealFftBackend fft;
    assert(fft.init());

    // Generate 3 seconds of 44.1 kHz sine wave at 1000 Hz
    uint32_t sample_rate = 44100;
    size_t num_frames = sample_rate * 3;
    std::vector<int16_t> pcm(num_frames);
    for (size_t f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / sample_rate;
        pcm[f] = static_cast<int16_t>(std::sin(2.0f * 3.14159265f * 1000.0f * t) * 20000.0f);
    }

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> asv_storage(1024 * 128, 0);
    MemoryWriter asv_writer(asv_storage.data(), asv_storage.size());

    SpectrumGenerator gen;
    assert(gen.init(pcm_reader, asv_writer, fft, sample_rate, 1, true, 64));
    assert(gen.generate_all());

    MemoryReader asv_reader(asv_storage.data(), asv_writer.size());
    SpectrumReader reader;
    assert(reader.init(asv_reader));

    // Getters validation
    assert(reader.duration_ms() == 3000);
    assert(reader.sample_rate() == 44100);
    assert(reader.channels() == 1);
    assert(reader.num_bands() == 64);
    assert(reader.hop_size() == DEFAULT_HOP_SIZE);
    assert(reader.lod_count() == 2);
    assert(reader.header().magic == ASV_MAGIC);
    assert(reader.header().version == ASV_VERSION);

    // Full-track overview query (should select LOD 1, since 3000ms / 10 = 300ms >= 90ms)
    std::vector<uint8_t> overview(10 * 64);
    size_t read_count = reader.read_spectrum(0, 3000, overview.data(), 10);
    assert(read_count == 10);

    // Verify 1000 Hz peak is present in every column of overview
    for (size_t col = 0; col < 10; ++col) {
        uint8_t max_val = 0;
        uint8_t peak_b = 0;
        for (size_t b = 0; b < 64; ++b) {
            uint8_t v = overview[col * 64 + b];
            if (v > max_val) {
                max_val = v;
                peak_b = static_cast<uint8_t>(b);
            }
        }
        // 1000 Hz in 64-band log filterbank should peak around band 32-37
        assert(peak_b >= 30 && peak_b <= 40);
        assert(max_val > 150);
    }

    // Zoomed-in query (should select LOD 0, since 100ms / 5 = 20ms < 90ms)
    std::vector<uint8_t> zoom(5 * 64);
    read_count = reader.read_spectrum(500, 100, zoom.data(), 5);
    assert(read_count == 5);
    for (size_t col = 0; col < 5; ++col) {
        uint8_t max_val = 0;
        uint8_t peak_b = 0;
        for (size_t b = 0; b < 64; ++b) {
            uint8_t v = zoom[col * 64 + b];
            if (v > max_val) {
                max_val = v;
                peak_b = static_cast<uint8_t>(b);
            }
        }
        assert(peak_b >= 30 && peak_b <= 40);
        assert(max_val > 150);
    }
}

void test_lod_selection() {
    AsvHeader hdr{};
    hdr.magic = ASV_MAGIC;
    hdr.version = ASV_VERSION;
    hdr.channels = 1;
    hdr.num_bands = 64;
    hdr.duration_ms = 10000;
    hdr.lod_count = 2;
    hdr.lods[0].frame_count = 1000;
    hdr.lods[0].downsample_ratio = 1;
    hdr.lods[0].file_offset = 128;
    hdr.lods[1].frame_count = 62;
    hdr.lods[1].downsample_ratio = 16;
    hdr.lods[1].file_offset = 128 + 1000 * 64;

    std::vector<uint8_t> buf(sizeof(hdr) + 1000 * 64 + 62 * 64, 0);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    MemoryReader mem(buf.data(), buf.size());
    SpectrumReader reader;
    assert(reader.init(mem));

    // Threshold is 90ms
    // 89ms per frame -> LOD 0
    assert(reader.select_lod(89, 1) == 0);
    assert(reader.select_lod(890, 10) == 0);

    // 90ms per frame -> LOD 1
    assert(reader.select_lod(90, 1) == 1);
    assert(reader.select_lod(900, 10) == 1);

    // >90ms per frame -> LOD 1
    assert(reader.select_lod(100, 1) == 1);
    assert(reader.select_lod(1000, 5) == 1);

    // Edge cases: 0 target frames or 0 duration
    assert(reader.select_lod(1000, 0) == 0);
    assert(reader.select_lod(0, 10) == 0);

    // If lod_count == 1
    hdr.lod_count = 1;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    mem.seek(0);
    SpectrumReader reader_single_lod;
    assert(reader_single_lod.init(mem));
    assert(reader_single_lod.select_lod(5000, 10) == 0);
}

void test_error_and_boundaries() {
    SpectrumReader reader;

    // Reading before init must return 0
    std::vector<uint8_t> out(64);
    assert(reader.read_spectrum(0, 100, out.data(), 1) == 0);

    // Corrupted magic
    AsvHeader bad_hdr{};
    bad_hdr.magic = 0x12345678;
    bad_hdr.version = ASV_VERSION;
    bad_hdr.channels = 1;
    bad_hdr.num_bands = 64;
    bad_hdr.lod_count = 2;
    MemoryReader bad_mem1(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem1));

    // Corrupted version
    bad_hdr.magic = ASV_MAGIC;
    bad_hdr.version = 99;
    MemoryReader bad_mem2(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem2));

    // Invalid channels
    bad_hdr.version = ASV_VERSION;
    bad_hdr.channels = 0;
    MemoryReader bad_mem3(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem3));

    bad_hdr.channels = 3;
    MemoryReader bad_mem4(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem4));

    // Invalid num_bands
    bad_hdr.channels = 1;
    bad_hdr.num_bands = 0;
    MemoryReader bad_mem5(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem5));

    bad_hdr.num_bands = 65;
    MemoryReader bad_mem6(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem6));

    // Invalid lod_count
    bad_hdr.num_bands = 64;
    bad_hdr.lod_count = 0;
    MemoryReader bad_mem7(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem7));

    bad_hdr.lod_count = 5;
    MemoryReader bad_mem8(reinterpret_cast<const uint8_t*>(&bad_hdr), sizeof(bad_hdr));
    assert(!reader.init(bad_mem8));

    // Valid header for boundary checks
    AsvHeader valid_hdr{};
    valid_hdr.magic = ASV_MAGIC;
    valid_hdr.version = ASV_VERSION;
    valid_hdr.channels = 1;
    valid_hdr.num_bands = 64;
    valid_hdr.duration_ms = 1000;
    valid_hdr.lod_count = 2;
    valid_hdr.lods[0].frame_count = 100;
    valid_hdr.lods[0].file_offset = 128;
    valid_hdr.lods[1].frame_count = 10;
    valid_hdr.lods[1].file_offset = 128 + 100 * 64;

    std::vector<uint8_t> valid_buf(sizeof(valid_hdr) + 100 * 64 + 10 * 64, 42);
    std::memcpy(valid_buf.data(), &valid_hdr, sizeof(valid_hdr));
    MemoryReader valid_mem(valid_buf.data(), valid_buf.size());
    assert(reader.init(valid_mem));

    // Null output buffer
    assert(reader.read_spectrum(0, 100, nullptr, 5) == 0);

    // Max frames == 0
    assert(reader.read_spectrum(0, 100, out.data(), 0) == 0);

    // Duration == 0
    assert(reader.read_spectrum(0, 0, out.data(), 5) == 0);

    // Start >= duration_ms
    assert(reader.read_spectrum(1000, 100, out.data(), 5) == 0);
    assert(reader.read_spectrum(1500, 100, out.data(), 5) == 0);

    // Start + duration > track duration should clamp duration and still read
    std::vector<uint8_t> clamped_out(5 * 64);
    assert(reader.read_spectrum(800, 500, clamped_out.data(), 5) == 5);
}

void test_sector_caching_and_straddling() {
    // Construct an ASV file where frames straddle 512-byte sector boundaries.
    // Header is 128 bytes. With num_bands = 48:
    // Frame 0: [128, 176)
    // ...
    // Frame 7: [128 + 7*48, 128 + 8*48) = [464, 512)
    // Frame 8: [512, 560)
    // ...
    // Let's set file_offset = 500 so Frame 0 is [500, 548), straddling 512!
    AsvHeader hdr{};
    hdr.magic = ASV_MAGIC;
    hdr.version = ASV_VERSION;
    hdr.channels = 1;
    hdr.num_bands = 48;
    hdr.duration_ms = 2000;
    hdr.lod_count = 1;
    hdr.lods[0].downsample_ratio = 1;
    hdr.lods[0].frame_count = 20;
    hdr.lods[0].file_offset = 500; // Frame 0 straddles 512-byte boundary [500, 548)

    std::vector<uint8_t> storage(2048, 0);
    std::memcpy(storage.data(), &hdr, sizeof(hdr));

    // Fill frames with recognizable pattern: frame f has bytes (f * 10 + b)
    for (uint32_t f = 0; f < 20; ++f) {
        for (uint32_t b = 0; b < 48; ++b) {
            storage[500 + f * 48 + b] = static_cast<uint8_t>((f * 7 + b) & 0xFF);
        }
    }

    MemoryReader mem(storage.data(), storage.size());
    SpectrumReader reader;
    assert(reader.init(mem));

    // Read frame 0 (which straddles sector 0 and sector 1: bytes [500..512) in sector 0, [512..548) in sector 1)
    std::vector<uint8_t> read_buf(48);
    // 20 frames across 2000ms = 100ms per frame.
    // Querying [0, 50) will select frame 0 only.
    size_t count = reader.read_spectrum(0, 50, read_buf.data(), 1);
    assert(count == 1);
    for (uint32_t b = 0; b < 48; ++b) {
        uint8_t expected = static_cast<uint8_t>((0 * 7 + b) & 0xFF);
        assert(read_buf[b] == expected);
    }

    // Now read frame 1 [548, 596), which is entirely inside sector 1.
    // It should hit the sector 1 cache loaded by the previous straddle!
    count = reader.read_spectrum(100, 50, read_buf.data(), 1);
    assert(count == 1);
    for (uint32_t b = 0; b < 48; ++b) {
        uint8_t expected = static_cast<uint8_t>((1 * 7 + b) & 0xFF);
        assert(read_buf[b] == expected);
    }

    // Read backwards to frame 0 again to verify seeking backwards across sectors
    count = reader.read_spectrum(0, 50, read_buf.data(), 1);
    assert(count == 1);
    for (uint32_t b = 0; b < 48; ++b) {
        uint8_t expected = static_cast<uint8_t>((0 * 7 + b) & 0xFF);
        assert(read_buf[b] == expected);
    }
}

void test_peak_hold_decimation() {
    // Test that decimating across multiple frames takes the peak (max) for each band
    AsvHeader hdr{};
    hdr.magic = ASV_MAGIC;
    hdr.version = ASV_VERSION;
    hdr.channels = 1;
    hdr.num_bands = 4;
    hdr.duration_ms = 1000;
    hdr.lod_count = 1;
    hdr.lods[0].frame_count = 10;
    hdr.lods[0].downsample_ratio = 1;
    hdr.lods[0].file_offset = 128;

    std::vector<uint8_t> storage(128 + 10 * 4, 0);
    std::memcpy(storage.data(), &hdr, sizeof(hdr));

    // 10 frames, each 100ms.
    // Frame 0: [10,  5,  2, 50]
    // Frame 1: [20, 80,  1, 10]
    // Frame 2: [ 5, 12, 99,  0]
    storage[128 + 0 * 4 + 0] = 10; storage[128 + 0 * 4 + 1] = 5;  storage[128 + 0 * 4 + 2] = 2;  storage[128 + 0 * 4 + 3] = 50;
    storage[128 + 1 * 4 + 0] = 20; storage[128 + 1 * 4 + 1] = 80; storage[128 + 1 * 4 + 2] = 1;  storage[128 + 1 * 4 + 3] = 10;
    storage[128 + 2 * 4 + 0] = 5;  storage[128 + 2 * 4 + 1] = 12; storage[128 + 2 * 4 + 2] = 99; storage[128 + 2 * 4 + 3] = 0;

    MemoryReader mem(storage.data(), storage.size());
    SpectrumReader reader;
    assert(reader.init(mem));

    // Query 0 to 300ms in 1 column -> should span frames 0, 1, 2
    std::vector<uint8_t> out(4);
    size_t n = reader.read_spectrum(0, 300, out.data(), 1);
    assert(n == 1);
    // Band 0 peak: max(10, 20, 5) = 20
    assert(out[0] == 20);
    // Band 1 peak: max(5, 80, 12) = 80
    assert(out[1] == 80);
    // Band 2 peak: max(2, 1, 99) = 99
    assert(out[2] == 99);
    // Band 3 peak: max(50, 10, 0) = 50
    assert(out[3] == 50);
}

int main() {
    test_basic_reading();
    test_lod_selection();
    test_error_and_boundaries();
    test_sector_caching_and_straddling();
    test_peak_hold_decimation();

    std::cout << "test_spectrum_reader PASSED\n";
    return 0;
}
