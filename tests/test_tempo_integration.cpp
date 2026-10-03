#include <audio_codecs/tempo.h>
#include <audio_codecs/spectrum.h>
#include <audio_codecs/preview/preview_stream.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace audio_codecs::tempo;
using namespace audio_codecs::spectrum;
using namespace audio_codecs::preview;

void test_e2e_pcm_spectrum_tempo_pipeline() {
    const uint32_t sample_rate = 44100;
    const uint32_t duration_sec = 10;
    const size_t total_samples = sample_rate * duration_sec;
    std::vector<int16_t> pcm_samples(total_samples, 0);

    // 120 BPM: Beat occurs every 0.5 s = 22050 samples
    const size_t beat_samples = 22050;
    for (size_t i = 0; i < total_samples; ++i) {
        size_t beat_offset = i % beat_samples;
        if (beat_offset < 1000) {
            // Kick drum: 60 Hz sine wave burst
            float t = static_cast<float>(beat_offset) / static_cast<float>(sample_rate);
            pcm_samples[i] = static_cast<int16_t>(20000.0f * std::sin(2.0f * 3.14159f * 60.0f * t));
        }
    }

    // Step 1: Generate ASV1 spectrum file
    MemoryReader pcm_reader(reinterpret_cast<const uint8_t*>(pcm_samples.data()), pcm_samples.size() * sizeof(int16_t));
    std::vector<uint8_t> asv_memory(65536, 0);
    MemoryWriter asv_writer(asv_memory.data(), asv_memory.size());

    SpectrumGenerator asv_gen;
    DesktopRealFftBackend fft_backend;
    bool init_fft = fft_backend.init();
    assert(init_fft);
    bool init_asv = asv_gen.init(pcm_reader, asv_writer, fft_backend, sample_rate, 1, false, 64);
    assert(init_asv);

    bool gen_asv = asv_gen.generate_all();
    assert(gen_asv);
    bool asv_complete = (asv_gen.status() == GeneratorStatus::Complete);
    assert(asv_complete);

    // Step 2: Generate ATT1 tempo file
    MemoryReader asv_reader(asv_memory.data(), asv_writer.size());
    std::vector<uint8_t> att_memory(16384, 0);
    MemoryWriter att_writer(att_memory.data(), att_memory.size());

    TempoGenerator tempo_gen;
    bool init_tempo = tempo_gen.init(&asv_reader, &att_writer);
    assert(init_tempo);

    while (tempo_gen.step(128)) {}
    bool tempo_complete = tempo_gen.is_complete();
    assert(tempo_complete);

    // Step 3: Query with TempoReader
    MemoryReader att_reader(att_memory.data(), att_writer.size());
    TempoReader reader;
    bool init_reader = reader.init(&att_reader);
    assert(init_reader);

    float bpm = reader.get_bpm_at(2000);
    assert(bpm >= 118.0f && bpm <= 122.0f);
    assert(reader.header().total_beats > 15);
}

int main() {
    test_e2e_pcm_spectrum_tempo_pipeline();
    std::cout << "test_tempo_integration PASSED\n";
    return 0;
}
