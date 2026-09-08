#include "audio_codecs/preview/preview_generator.h"
#include "audio_codecs/preview/preview_stream.h"
#include <cassert>
#include <vector>
#include <iostream>

using namespace audio_codecs::preview;

void test_stereo_impulse() {
    // Generate 512 chunks of stereo audio (512 * 128 = 65,536 sample frames)
    // Put an isolated full-scale spike at frame 200 on Left channel (+32767)
    constexpr size_t total_frames = 512 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 0);
    pcm[200 * 2] = 32767;     // Left spike at frame 200 (in chunk 1: 128..255)
    pcm[200 * 2 + 1] = -32768;// Right trough at frame 200

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> out_buf(128 + 512 * 4 + 32 * 4 + 2 * 4 + 512, 0);
    MemoryWriter out_writer(out_buf.data(), out_buf.size());

    PreviewGenerator generator;
    bool ok = generator.init(pcm_reader, out_writer, nullptr, 44100, 2, true);
    assert(ok);

    bool done = generator.generate_all();
    assert(done);

    const ApvHeader& hdr = generator.header();
    assert(hdr.magic == APV_MAGIC);
    assert(hdr.total_pcm_frames == total_frames);
    assert(hdr.lods[0].chunk_count == 512);
    assert(hdr.lods[1].chunk_count == 32);  // 512 / 16 = 32
    assert(hdr.lods[2].chunk_count == 2);   // 32 / 16 = 2

    // Check LOD 0 spike in chunk 1
    const uint8_t* p = out_buf.data() + hdr.lods[0].file_offset;
    const WaveformPointStereo* lod0 = reinterpret_cast<const WaveformPointStereo*>(p);
    assert(lod0[0].left_max == 0 && lod0[0].right_min == 0); // chunk 0 silence
    assert(lod0[1].left_max == 127);  // chunk 1 has spike
    assert(lod0[1].right_min == -128); // chunk 1 has negative trough

    // Check LOD 1 spike (chunk 1 falls into LOD 1 chunk 0)
    const WaveformPointStereo* lod1 = reinterpret_cast<const WaveformPointStereo*>(out_buf.data() + hdr.lods[1].file_offset);
    assert(lod1[0].left_max == 127);
    assert(lod1[0].right_min == -128);

    // Check LOD 2 spike (chunk 1 falls into LOD 2 chunk 0)
    const WaveformPointStereo* lod2 = reinterpret_cast<const WaveformPointStereo*>(out_buf.data() + hdr.lods[2].file_offset);
    assert(lod2[0].left_max == 127);
    assert(lod2[0].right_min == -128);
}

void test_cooperative_stepping_and_progress() {
    constexpr size_t total_frames = 256 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 100);

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> out_buf(128 + 256 * 4 + 16 * 4 + 1 * 4 + 128, 0);
    MemoryWriter out_writer(out_buf.data(), out_buf.size());

    PreviewGenerator generator;
    assert(generator.init(pcm_reader, out_writer, nullptr, 48000, 2, true));
    assert(generator.progress() == 0.0f);

    size_t step_count = 0;
    while (true) {
        GeneratorStatus status = generator.step(64);
        if (status == GeneratorStatus::Complete) break;
        assert(status == GeneratorStatus::Working);
        step_count++;
        assert(generator.progress() >= 0.0f && generator.progress() <= 100.0f);
    }
    assert(step_count > 0);
    assert(generator.progress() == 100.0f);

    const ApvHeader& hdr = generator.header();
    assert(hdr.total_pcm_frames == total_frames);
    assert(hdr.sample_rate == 48000);
    assert(hdr.lods[0].chunk_count == 256);
    assert(hdr.lods[1].chunk_count == 16);
    assert(hdr.lods[2].chunk_count == 1);
}

void test_mono_partial_chunks() {
    // 150 frames = 1 full chunk (128) + 1 partial chunk (22)
    constexpr size_t total_frames = 150;
    std::vector<int16_t> pcm(total_frames, 0);
    pcm[50] = 20000;
    pcm[135] = -25000;

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> out_buf(512, 0);
    MemoryWriter out_writer(out_buf.data(), out_buf.size());

    PreviewGenerator generator;
    assert(generator.init(pcm_reader, out_writer, nullptr, 22050, 1, false));
    assert(generator.generate_all());

    const ApvHeader& hdr = generator.header();
    assert(hdr.channels == 1);
    assert(hdr.bytes_per_chunk == 2);
    assert((hdr.flags & APV_FLAG_STEREO) == 0);
    assert(hdr.total_pcm_frames == 150);
    assert(hdr.lods[0].chunk_count == 2);
    assert(hdr.lods[1].chunk_count == 1);
    assert(hdr.lods[2].chunk_count == 1);

    const WaveformPointMono* lod0 = reinterpret_cast<const WaveformPointMono*>(out_buf.data() + hdr.lods[0].file_offset);
    assert(lod0[0].max > 0);
    assert(lod0[1].min < 0);
}

// Mock writer that does NOT implement SeekableReader
class WriteOnlyWriter : public SeekableWriter {
public:
    size_t write(const uint8_t*, size_t bytes) override { return bytes; }
    bool seek(uint64_t) override { return true; }
    uint64_t position() const override { return 0; }
    uint64_t size() const override { return 0; }
    void flush() override {}
};

void test_invalid_destination() {
    std::vector<int16_t> pcm(256, 0);
    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    WriteOnlyWriter write_only;

    PreviewGenerator generator;
    // Must fail init because write_only is not a SeekableReader
    assert(!generator.init(pcm_reader, write_only, nullptr, 44100, 2, true));
}

// Mock reader simulating premature EOF error
class PrematureEofReader : public SeekableReader {
public:
    size_t read(uint8_t*, size_t) override { return 0; } // Returns 0 despite size > pos
    bool seek(uint64_t) override { return true; }
    uint64_t position() const override { return 500; }
    uint64_t size() const override { return 2000; } // pos < size -> premature EOF
};

void test_premature_eof_error() {
    PrematureEofReader bad_reader;
    std::vector<uint8_t> out_buf(512, 0);
    MemoryWriter out_writer(out_buf.data(), out_buf.size());

    PreviewGenerator generator;
    assert(generator.init(bad_reader, out_writer, nullptr, 44100, 2, true));

    // Step 1: Init state -> Working
    GeneratorStatus s1 = generator.step(64);
    assert(s1 == GeneratorStatus::Working);

    // Step 2: DecodeLOD0 -> premature EOF -> ErrorSource
    GeneratorStatus s2 = generator.step(64);
    assert(s2 == GeneratorStatus::ErrorSource);

    // Subsequent steps remain in Error state
    GeneratorStatus s3 = generator.step(64);
    assert(s3 == GeneratorStatus::ErrorDest);
}

void test_sliced_lod_reduction() {
    // 512 chunks = 32 LOD1 chunks. With budget 4, LOD1 takes 8 steps.
    constexpr size_t total_frames = 512 * 128;
    std::vector<int16_t> pcm(total_frames * 2, 50);

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> out_buf(128 + 512 * 4 + 32 * 4 + 2 * 4 + 512, 0);
    MemoryWriter out_writer(out_buf.data(), out_buf.size());

    PreviewGenerator generator;
    assert(generator.init(pcm_reader, out_writer, nullptr, 44100, 2, true));

    // Finish Init
    assert(generator.step(1000) == GeneratorStatus::Working);
    // Finish LOD 0
    assert(generator.step(1000) == GeneratorStatus::Working);

    // Now in GenerateLOD1: step with small budget (4 groups per step)
    size_t lod1_yields = 0;
    while (generator.header().lods[1].chunk_count == 0) {
        GeneratorStatus s = generator.step(4);
        assert(s == GeneratorStatus::Working);
        lod1_yields++;
    }
    // 32 groups / 4 = 8 steps to complete LOD 1
    assert(lod1_yields >= 8);
    assert(generator.header().lods[1].chunk_count == 32);

    // Finish remainder
    assert(generator.generate_all());
    assert(generator.header().lods[2].chunk_count == 2);
}

int main() {
    test_stereo_impulse();
    test_cooperative_stepping_and_progress();
    test_mono_partial_chunks();
    test_invalid_destination();
    test_premature_eof_error();
    test_sliced_lod_reduction();

    std::cout << "test_preview_generator PASSED\n";
    return 0;
}
