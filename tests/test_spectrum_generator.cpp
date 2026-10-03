#include "audio_codecs/spectrum/spectrum_generator.h"
#include "audio_codecs/spectrum/desktop_fft.h"
#include "audio_codecs/preview/preview_stream.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>

using namespace audio_codecs::spectrum;
using namespace audio_codecs::preview;

void test_basic_generation() {
    DesktopRealFftBackend fft;
    bool ok = fft.init();
    assert(ok);

    // Generate 2 seconds of 44.1 kHz stereo audio with 440 Hz sine wave
    uint32_t sample_rate = 44100;
    size_t num_frames = sample_rate * 2;
    std::vector<int16_t> pcm(num_frames * 2);
    for (size_t f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / sample_rate;
        int16_t val = static_cast<int16_t>(std::sin(2.0f * 3.14159265f * 440.0f * t) * 20000.0f);
        pcm[f * 2] = val;
        pcm[f * 2 + 1] = val;
    }

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> asv_storage(1024 * 128, 0);
    MemoryWriter asv_writer(asv_storage.data(), asv_storage.size());

    SpectrumGenerator gen;
    ok = gen.init(pcm_reader, asv_writer, fft, sample_rate, 2, true, 64);
    assert(ok);
    ok = gen.generate_all();
    assert(ok);

    assert(gen.status() == GeneratorStatus::Complete);
    assert(gen.progress() >= 1.0f);

    // Validate generated header
    const AsvHeader* hdr = reinterpret_cast<const AsvHeader*>(asv_storage.data());
    assert(hdr->magic == ASV_MAGIC);
    assert(hdr->version == 1);
    assert(hdr->channels == 1);
    assert(hdr->num_bands == 64);
    assert(hdr->duration_ms == 2000);
    assert(hdr->lod_count == 2);
    assert(hdr->lods[0].frame_count > 0);
    assert(hdr->lods[1].frame_count > 0);
    assert(hdr->lods[1].downsample_ratio == 16);

    // Validate LOD 1 peak-hold correctness:
    // Every LOD 1 frame must equal the maximum of corresponding 16 (or remainder) LOD 0 frames
    const uint8_t* lod0_data = asv_storage.data() + hdr->lods[0].file_offset;
    const uint8_t* lod1_data = asv_storage.data() + hdr->lods[1].file_offset;

    for (uint32_t g = 0; g < hdr->lods[1].frame_count; ++g) {
        uint32_t start_f = g * 16;
        uint32_t count = std::min(16u, hdr->lods[0].frame_count - start_f);
        for (uint8_t b = 0; b < hdr->num_bands; ++b) {
            uint8_t expected_max = 0;
            for (uint32_t i = 0; i < count; ++i) {
                expected_max = std::max(expected_max, lod0_data[(start_f + i) * hdr->num_bands + b]);
            }
            assert(lod1_data[g * hdr->num_bands + b] == expected_max);
        }
    }

    // Verify 440 Hz spectral peak in middle frame
    const uint8_t* mid_frame = lod0_data + (hdr->lods[0].frame_count / 2) * hdr->num_bands;
    uint8_t max_val = 0;
    uint8_t peak_b = 0;
    for (uint8_t b = 0; b < hdr->num_bands; ++b) {
        if (mid_frame[b] > max_val) {
            max_val = mid_frame[b];
            peak_b = b;
        }
    }
    // 440 Hz should peak around band 27 (log scale 20 Hz to 20000 Hz)
    assert(peak_b >= 25 && peak_b <= 29);
    assert(max_val > 180);

    std::cout << "test_spectrum_generator PASSED (LOD0=" << hdr->lods[0].frame_count
              << ", LOD1=" << hdr->lods[1].frame_count << ")\n";
}

void test_cooperative_stepping() {
    DesktopRealFftBackend fft;
    bool ok = fft.init();
    assert(ok);

    uint32_t sample_rate = 44100;
    size_t num_frames = sample_rate; // 1 second
    std::vector<int16_t> pcm(num_frames, 5000); // 1 channel

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> asv_storage(1024 * 64, 0);
    MemoryWriter asv_writer(asv_storage.data(), asv_storage.size());

    SpectrumGenerator gen;
    assert(gen.progress() == 0.0f);
    ok = gen.init(pcm_reader, asv_writer, fft, sample_rate, 1, false, 32);
    assert(ok);

    bool saw_lod0 = false;
    bool saw_lod1 = false;
    float prev_progress = -1.0f;

    while (gen.status() != GeneratorStatus::Complete) {
        GeneratorStatus st = gen.status();
        if (st == GeneratorStatus::ProcessingLOD0) saw_lod0 = true;
        if (st == GeneratorStatus::ProcessingLOD1) saw_lod1 = true;

        float p = gen.progress();
        assert(p >= prev_progress);
        prev_progress = p;

        gen.step(8); // Small budget of 8 frames
    }

    assert(saw_lod0);
    assert(saw_lod1);
    assert(gen.status() == GeneratorStatus::Complete);
    assert(gen.progress() >= 1.0f);

    const AsvHeader* hdr = reinterpret_cast<const AsvHeader*>(asv_storage.data());
    assert(hdr->channels == 1);
    assert(hdr->num_bands == 32);
    assert(hdr->duration_ms == 1000);
}

// Mock writer that does NOT inherit from SeekableReader
class WriteOnlyWriter : public SeekableWriter {
public:
    size_t write(const uint8_t*, size_t bytes) override { return bytes; }
    bool seek(uint64_t) override { return true; }
    uint64_t position() const override { return 128; }
    uint64_t size() const override { return 128; }
    void flush() override {}
};

void test_error_conditions() {
    DesktopRealFftBackend fft;
    bool ok = fft.init();
    assert(ok);

    std::vector<int16_t> pcm(1024, 0);
    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> asv_storage(1024, 0);
    MemoryWriter asv_writer(asv_storage.data(), asv_storage.size());

    SpectrumGenerator gen;

    // generate_all before init must fail
    ok = gen.generate_all();
    assert(!ok);

    // Invalid channel counts must fail init
    ok = gen.init(pcm_reader, asv_writer, fft, 44100, 0);
    assert(!ok);
    ok = gen.init(pcm_reader, asv_writer, fft, 44100, 3);
    assert(!ok);

    // Zero sample rate must fail init
    ok = gen.init(pcm_reader, asv_writer, fft, 0, 1);
    assert(!ok);

    // Stereo without downmix to mono must fail init
    ok = gen.init(pcm_reader, asv_writer, fft, 44100, 2, false);
    assert(!ok);
    assert(gen.status() == GeneratorStatus::ErrorSource);

    // Write-only destination: init succeeds, LOD 0 finishes, but LOD 1 fails with ErrorDest
    WriteOnlyWriter write_only;
    ok = gen.init(pcm_reader, write_only, fft, 44100, 1);
    assert(ok);
    ok = gen.generate_all();
    assert(!ok);
    assert(gen.status() == GeneratorStatus::ErrorDest);
}

int main() {
    test_basic_generation();
    test_cooperative_stepping();
    test_error_conditions();
    return 0;
}
