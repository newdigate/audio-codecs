#include "audio_codecs/audio_codecs.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

using namespace audio_codecs;
using namespace audio_codecs::preview;

int main() {
    constexpr uint32_t sample_rate = 44100;
    constexpr size_t num_frames = sample_rate * 5;
    std::vector<int16_t> pcm(num_frames * 2);
    for (size_t f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / sample_rate;
        float env = 1.0f - (static_cast<float>(f) / num_frames);
        int16_t val = static_cast<int16_t>(std::sin(2.0f * 3.14159f * 440.0f * t) * 30000.0f * env);
        pcm[f * 2] = val;
        pcm[f * 2 + 1] = -val;
    }

    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm.data()), pcm.size() * sizeof(int16_t));
    std::vector<uint8_t> apv_storage(1024 * 64, 0);
    MemoryWriter apv_writer(apv_storage.data(), apv_storage.size());

    PreviewGenerator gen;
    bool ok = gen.init(pcm_reader, apv_writer, nullptr, sample_rate, 2, true);
    assert(ok);
    assert(gen.generate_all());

    MemoryReader apv_reader(apv_storage.data(), apv_writer.size());
    PreviewReader pr;
    assert(pr.init(apv_reader));
    assert(pr.duration_ms() == 5000);

    WaveformPointStereo points[10];
    assert(pr.read_preview_stereo(0, 5000, points, 10) == 10);
    for (size_t i = 1; i < 10; ++i) {
        assert(points[i].left_max <= points[i - 1].left_max);
    }

    std::cout << "test_preview_integration PASSED\n";
    return 0;
}
