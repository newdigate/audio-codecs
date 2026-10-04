#include <audio_codecs/slice/slice_reader.h>
#include <audio_codecs/slice/slice_generator.h>
#include <cassert>
#include <vector>
#include <cstring>
#include <iostream>

using namespace audio_codecs::slice;

void test_reader_uninitialized() {
    SliceReader reader;
    assert(reader.total_slices() == 0);
    assert(reader.header().magic == 0);
    assert(reader.header().sample_rate == 0);
    assert(reader.get_slice(0) == nullptr);
    assert(reader.find_slice_at_sample(100) == nullptr);
    assert(reader.find_slice_at_ms(100) == nullptr);
    assert(reader.find_slice_at_tick(100) == nullptr);
}

void test_reader_init_and_validation() {
    SliceReader reader;

    // Reject nullptr and too small buffer
    bool ok = reader.init(nullptr, 0);
    assert(!ok);
    (void)ok;

    uint8_t garbage[64] = {0};
    ok = reader.init(garbage, sizeof(garbage));
    assert(!ok);

    uint8_t small_buf[127] = {0};
    ok = reader.init(small_buf, sizeof(small_buf));
    assert(!ok);

    // Header size exactly 128, but invalid magic
    AslHeader hdr{};
    hdr.magic = 0x12345678;
    hdr.version = ASL_VERSION;
    hdr.header_size = 128;
    hdr.slices_offset = 128;
    hdr.total_slices = 0;
    ok = reader.init(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    assert(!ok);

    // Invalid version
    hdr.magic = ASL_MAGIC;
    hdr.version = 99;
    ok = reader.init(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    assert(!ok);

    // Invalid header_size
    hdr.version = ASL_VERSION;
    hdr.header_size = 64;
    ok = reader.init(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    assert(!ok);

    // Invalid slices_offset (< 128)
    hdr.header_size = 128;
    hdr.slices_offset = 64;
    ok = reader.init(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    assert(!ok);

    // Buffer smaller than required slices size
    hdr.slices_offset = 128;
    hdr.total_slices = 5;
    ok = reader.init(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)); // only 128 bytes, need 128 + 5*32 = 288
    assert(!ok);

    // Zero slices valid header
    hdr.total_slices = 0;
    ok = reader.init(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr));
    assert(ok);
    assert(reader.total_slices() == 0);
    assert(reader.get_slice(0) == nullptr);
    assert(reader.find_slice_at_sample(0) == nullptr);
    assert(reader.find_slice_at_ms(0) == nullptr);
    assert(reader.find_slice_at_tick(0) == nullptr);

    // Re-init with bad data resets state
    ok = reader.init(garbage, sizeof(garbage));
    assert(!ok);
    assert(reader.total_slices() == 0);
    assert(reader.header().magic == 0);
}

void test_reader_lookups() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_GRID;
    cfg.sample_rate = 44100;
    cfg.bpm_q16 = 120 << 16;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    std::vector<int16_t> silence(88200, 0); // 2 seconds, 16 sixteenths
    ok = gen.process_pcm(silence.data(), static_cast<uint32_t>(silence.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    uint8_t buffer[2048];
    size_t written = 0;
    ok = gen.serialize(buffer, sizeof(buffer), &written);
    assert(ok);

    SliceReader reader;
    ok = reader.init(buffer, written);
    assert(ok);

    assert(reader.total_slices() == 16);
    assert(reader.header().sample_rate == 44100);

    // O(1) indexed lookup
    const AslSlice* s0 = reader.get_slice(0);
    assert(s0 != nullptr && s0->start_sample == 0);
    (void)s0;
    const AslSlice* s15 = reader.get_slice(15);
    assert(s15 != nullptr);
    (void)s15;
    const AslSlice* s_invalid = reader.get_slice(16);
    assert(s_invalid == nullptr);
    s_invalid = reader.get_slice(100);
    assert(s_invalid == nullptr);
    (void)s_invalid;

    // O(log N) binary search by sample
    const AslSlice* s_sample = reader.find_slice_at_sample(6000); // 16th note 1 starts at 5512
    assert(s_sample != nullptr);
    assert(s_sample->subdivision == 1);
    (void)s_sample;

    // Search by sample beyond last slice length
    const AslSlice* s_beyond = reader.find_slice_at_sample(999999);
    assert(s_beyond == nullptr);
    (void)s_beyond;

    // O(log N) binary search by ms
    const AslSlice* s_ms = reader.find_slice_at_ms(550); // ~0.55s is beat 1, subdivision 0 (starts at 500ms)
    assert(s_ms != nullptr);
    assert(s_ms->beat_within_bar == 1);
    (void)s_ms;

    // Search by ms beyond range
    const AslSlice* s_ms_beyond = reader.find_slice_at_ms(5000); // 5 sec > 2 sec
    assert(s_ms_beyond == nullptr);
    (void)s_ms_beyond;

    // O(log N) binary search by musical tick
    const AslSlice* s_tick = reader.find_slice_at_tick(250); // 240 is 16th note 2
    assert(s_tick != nullptr);
    assert(s_tick->musical_tick == 240);
    (void)s_tick;

    // Tick search at tick 0
    const AslSlice* s_tick0 = reader.find_slice_at_tick(0);
    assert(s_tick0 != nullptr);
    assert(s_tick0->musical_tick == 0);
    (void)s_tick0;
}

void test_reader_sparse_slices() {
    // Test slice reader where first slice does not start at 0
    uint8_t buffer[512] = {0};
    auto* hdr = reinterpret_cast<AslHeader*>(buffer);
    hdr->magic = ASL_MAGIC;
    hdr->version = ASL_VERSION;
    hdr->header_size = sizeof(AslHeader);
    hdr->slices_offset = sizeof(AslHeader);
    hdr->total_slices = 2;
    hdr->sample_rate = 44100;

    auto* slices = reinterpret_cast<AslSlice*>(buffer + sizeof(AslHeader));
    // Slice 0: start 1000, length 500, tick 120
    slices[0].start_sample = 1000;
    slices[0].length_samples = 500;
    slices[0].musical_tick = 120;

    // Slice 1: start 2000, length 500, tick 240
    slices[1].start_sample = 2000;
    slices[1].length_samples = 500;
    slices[1].musical_tick = 240;

    SliceReader reader;
    bool ok = reader.init(buffer, sizeof(AslHeader) + 2 * sizeof(AslSlice));
    assert(ok);
    (void)ok;

    // Sample before first slice
    assert(reader.find_slice_at_sample(500) == nullptr);
    // Sample inside slice 0
    const AslSlice* s = reader.find_slice_at_sample(1200);
    assert(s != nullptr && s->start_sample == 1000);
    // Sample in gap between slice 0 and slice 1
    assert(reader.find_slice_at_sample(1600) == nullptr);
    // Sample inside slice 1
    s = reader.find_slice_at_sample(2100);
    assert(s != nullptr && s->start_sample == 2000);
    // Sample after slice 1
    assert(reader.find_slice_at_sample(2600) == nullptr);

    // Tick before first slice
    assert(reader.find_slice_at_tick(50) == nullptr);
    // Tick between slice 0 and slice 1 -> returns slice 0
    s = reader.find_slice_at_tick(150);
    assert(s != nullptr && s->start_sample == 1000);
    // Tick at slice 1
    s = reader.find_slice_at_tick(240);
    assert(s != nullptr && s->start_sample == 2000);
    // Tick after slice 1
    s = reader.find_slice_at_tick(300);
    assert(s != nullptr && s->start_sample == 2000);
    (void)s;
}

int main() {
    test_reader_uninitialized();
    test_reader_init_and_validation();
    test_reader_lookups();
    test_reader_sparse_slices();
    std::cout << "test_slice_reader PASSED\n";
    return 0;
}
