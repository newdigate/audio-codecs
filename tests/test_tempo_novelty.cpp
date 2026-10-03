#include <audio_codecs/tempo/tempo_novelty.h>
#include <cassert>
#include <iostream>

using namespace audio_codecs::tempo;

void test_subband_flux_separation() {
    TempoNoveltyExtractor extractor;
    extractor.reset();

    uint8_t frame0[64] = {0};
    NoveltySample s0 = extractor.process_frame(frame0);
    assert(s0.novelty == 0.0f);
    assert(s0.bass_flux == 0.0f);
    assert(s0.snare_flux == 0.0f);
    assert(s0.high_flux == 0.0f);

    // Kick transient (energy burst in bands 2..5)
    uint8_t frame1[64] = {0};
    for (int b = 2; b <= 5; ++b) frame1[b] = 200;
    NoveltySample s1 = extractor.process_frame(frame1);
    assert(s1.bass_flux > 500.0f);
    assert(s1.snare_flux == 0.0f);
    assert(s1.high_flux == 0.0f);
    assert(s1.novelty > 0.0f);

    // Snare transient (energy burst in bands 20..30)
    uint8_t frame2[64] = {0};
    for (int b = 20; b <= 30; ++b) frame2[b] = 180;
    NoveltySample s2 = extractor.process_frame(frame2);
    assert(s2.snare_flux > 500.0f);
    assert(s2.bass_flux == 0.0f);
    assert(s2.high_flux == 0.0f);

    // Hi-hat / cymbal transient (energy burst in bands 43..63)
    uint8_t frame3[64] = {0};
    for (int b = 43; b <= 63; ++b) frame3[b] = 160;
    NoveltySample s3 = extractor.process_frame(frame3);
    assert(s3.high_flux > 500.0f);
    assert(s3.bass_flux == 0.0f);
    assert(s3.snare_flux == 0.0f);
    assert(s3.raw_novelty > 0.0f);
}

void test_adaptive_threshold_suppresses_dc() {
    TempoNoveltyExtractor extractor;
    extractor.reset();

    // Constant tone over 20 frames should result in novelty falling back to 0
    uint8_t frame[64];
    for (int b = 0; b < 64; ++b) frame[b] = 150;

    extractor.process_frame(frame); // transient onset
    for (int i = 0; i < 20; ++i) {
        NoveltySample s = extractor.process_frame(frame);
        // Once steady, half-wave rectified delta is 0
        assert(s.novelty == 0.0f);
    }
}

int main() {
    test_subband_flux_separation();
    test_adaptive_threshold_suppresses_dc();
    std::cout << "test_tempo_novelty PASSED\n";
    return 0;
}
