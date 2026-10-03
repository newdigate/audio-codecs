#include "audio_codecs/audio_codecs.h"
#include <cassert>
#include <vector>
#include <cmath>
#include <iostream>

using namespace audio_codecs;
using namespace audio_codecs::spectrum;
using namespace audio_codecs::preview;

int main() {
    DesktopRealFftBackend fft;
    bool ok = fft.init();
    assert(ok);

    // Generate 4 seconds of audio:
    // First 2 seconds: 440 Hz tone (low-mid band)
    // Last 2 seconds: 4000 Hz tone (high band)
    uint32_t sample_rate = 44100;
    size_t num_frames = sample_rate * 4;
    std::vector<int16_t> pcm(num_frames * 2);

    for (size_t f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / sample_rate;
        float freq = (f < sample_rate * 2) ? 440.0f : 4000.0f;
        int16_t val = static_cast<int16_t>(std::sin(2.0f * 3.14159265f * freq * t) * 20000.0f);
        pcm[f * 2] = val;
        pcm[f * 2 + 1] = val;
    }

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> asv_storage(1024 * 256, 0);
    MemoryWriter asv_writer(asv_storage.data(), asv_storage.size());

    SpectrumGenerator gen;
    ok = gen.init(pcm_reader, asv_writer, fft, sample_rate, 2, true, 64);
    assert(ok);
    ok = gen.generate_all();
    assert(ok);

    MemoryReader asv_reader(asv_storage.data(), asv_writer.size());
    SpectrumReader reader;
    ok = reader.init(asv_reader);
    assert(ok);
    assert(reader.duration_ms() == 4000);

    // Query first half (0 to 2000 ms): peak should be around band 20-35 (440 Hz)
    std::vector<uint8_t> part1(64);
    reader.read_spectrum(500, 50, part1.data(), 1);
    size_t peak1 = 0;
    for (size_t b = 1; b < 64; ++b) {
        if (part1[b] > part1[peak1]) peak1 = b;
    }
    assert(peak1 >= 20 && peak1 <= 35);

    // Query second half (2000 to 4000 ms): peak should be around band 45-55 (4000 Hz)
    std::vector<uint8_t> part2(64);
    reader.read_spectrum(2500, 50, part2.data(), 1);
    size_t peak2 = 0;
    for (size_t b = 1; b < 64; ++b) {
        if (part2[b] > part2[peak2]) peak2 = b;
    }
    assert(peak2 >= 45 && peak2 <= 55);

    std::cout << "test_spectrum_integration PASSED (part1_peak=" << peak1 << ", part2_peak=" << peak2 << ")\n";
    return 0;
}
