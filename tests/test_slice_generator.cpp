#include <audio_codecs/slice/slice_generator.h>
#include <cassert>
#include <vector>
#include <cstring>
#include <iostream>

using namespace audio_codecs::slice;

void test_generator_cooperative_stepping() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_GRID;
    cfg.sample_rate = 44100;
    cfg.bpm_q16 = 120 << 16;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    std::vector<int16_t> silence(44100, 0); // 1 sec
    ok = gen.process_pcm(silence.data(), static_cast<uint32_t>(silence.size()));
    assert(ok);

    assert(!gen.is_complete());
    ok = gen.step(500); // 500 us budget
    assert(ok);
    assert(gen.is_complete());
}

void test_generator_buffer_saturation() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_TRANSIENT;
    cfg.max_slices = 10;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    // Feed 30 impulses
    std::vector<int16_t> pcm(44100, 0);
    for (int i = 0; i < 30; ++i) {
        int idx = i * 1000;
        pcm[idx] = 25000;
        pcm[idx + 1] = -20000;
    }

    ok = gen.process_pcm(pcm.data(), static_cast<uint32_t>(pcm.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    // Max slices must be bounded at 10
    assert(gen.slice_count() <= 10);
}

void test_generator_serialization() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_GRID;
    cfg.sample_rate = 44100;
    cfg.bpm_q16 = 120 << 16;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    std::vector<int16_t> silence(44100, 0); // 8 sixteenth-notes
    ok = gen.process_pcm(silence.data(), static_cast<uint32_t>(silence.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    uint8_t buffer[2048];
    size_t written = 0;
    ok = gen.serialize(buffer, sizeof(buffer), &written);
    assert(ok);
    assert(written == sizeof(AslHeader) + gen.slice_count() * sizeof(AslSlice));

    const AslHeader* hdr = reinterpret_cast<const AslHeader*>(buffer);
    assert(hdr->magic == ASL_MAGIC);
    assert(hdr->version == ASL_VERSION);
    assert(hdr->total_slices == gen.slice_count());
    assert(hdr->slices_offset == 128);
    (void)hdr;

    // Serialization edge cases
    size_t too_small = sizeof(AslHeader) + gen.slice_count() * sizeof(AslSlice) - 1;
    bool small_ok = gen.serialize(buffer, too_small, &written);
    assert(!small_ok);
    (void)small_ok;

    bool null_ok = gen.serialize(nullptr, sizeof(buffer), &written);
    assert(!null_ok);
    (void)null_ok;
}

void test_generator_reset_retains_valid_header() {
    SliceGenerator gen;
    // Default constructor should populate header template fields
    const AslHeader& h0 = gen.header();
    assert(h0.magic == ASL_MAGIC);
    assert(h0.version == ASL_VERSION);
    assert(h0.header_size == 128);
    assert(h0.sample_rate == 44100);
    assert(h0.channels == 2);
    assert(h0.time_signature_num == 4);
    assert(h0.time_signature_denom == 4);
    assert(h0.original_bpm_q16 == (120 << 16));
    assert(h0.ppqn == ASL_PPQN);
    assert(h0.slice_descriptor_size == sizeof(AslSlice));
    assert(h0.slices_offset == 128);
    (void)h0;

    // reset() without init() must retain a valid header
    gen.reset();
    const AslHeader& h1 = gen.header();
    assert(h1.magic == ASL_MAGIC);
    assert(h1.version == ASL_VERSION);
    assert(h1.header_size == 128);
    assert(h1.sample_rate == 44100);
    assert(h1.channels == 2);
    assert(h1.original_bpm_q16 == (120 << 16));
    (void)h1;

    // After custom init(), reset() must retain the custom config
    GeneratorConfig cfg;
    cfg.sample_rate = 48000;
    cfg.channels = 1;
    cfg.bpm_q16 = 96 << 16;
    cfg.mode = ASL_MODE_TRANSIENT;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    gen.reset();
    const AslHeader& h2 = gen.header();
    assert(h2.magic == ASL_MAGIC);
    assert(h2.sample_rate == 48000);
    assert(h2.channels == 1);
    assert(h2.original_bpm_q16 == (96 << 16));
    assert(h2.slice_mode == ASL_MODE_TRANSIENT);
    (void)h2;
}

void test_generator_long_slice_decay_no_overflow() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_TRANSIENT;
    cfg.sample_rate = 44100;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    // Transient at index 0, total samples 5,000,000 (~113s)
    // 5,000,000 * 1000 = 5,000,000,000 which overflows uint32_t (4,294,967,295)
    int16_t single_impulse[64] = {0};
    single_impulse[0] = 25000;
    single_impulse[1] = -20000;
    ok = gen.process_pcm(single_impulse, 64);
    assert(ok);

    std::vector<int16_t> silence(5000000 - 64, 0);
    ok = gen.process_pcm(silence.data(), static_cast<uint32_t>(silence.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    assert(gen.slice_count() == 1);
    const AslSlice* s = gen.get_slice(0);
    assert(s != nullptr);
    assert(s->length_samples == 5000000);
    // uint64_t calculation: 5000000 * 1000 / 44100 = 113378 ms, capped at 65535
    // If not overflowing uint32_t, decay_ms should be 65535 (not wrapped via 32-bit truncation to 15987)
    assert(s->decay_ms == 65535);
    (void)s;
}

int main() {
    test_generator_cooperative_stepping();
    test_generator_buffer_saturation();
    test_generator_serialization();
    test_generator_reset_retains_valid_header();
    test_generator_long_slice_decay_no_overflow();
    std::cout << "test_slice_generator PASSED\n";
    return 0;
}
