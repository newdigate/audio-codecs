#include <audio_codecs/tempo/tempo_inducer.h>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace audio_codecs::tempo;

void test_tempo_induction_120bpm() {
    TempoInducer inducer;
    inducer.reset(86.133f); // 44100 / 512

    // At 120 BPM, interval is 0.5 sec -> 0.5 * 86.133 = 43.066 frames
    float period_frames = 86.133f * 0.5f;

    // Feed 512 frames containing pulses every 43.066 frames
    float next_pulse = 0.0f;
    for (int f = 0; f < 512; ++f) {
        float novelty = 0.0f;
        if (std::fabs(static_cast<float>(f) - next_pulse) < 0.6f) {
            novelty = 100.0f;
            next_pulse += period_frames;
        }
        inducer.feed_sample(novelty);
    }

    TempoEstimate est = inducer.estimate_tempo();
    assert(est.bpm >= 119.0f && est.bpm <= 121.0f);
    assert(est.confidence > 100);
}

void test_octave_disambiguation() {
    TempoInducer inducer;
    inducer.reset(86.133f);

    // Feed 130 BPM with some half-tempo subharmonics
    float period_frames = 86.133f * (60.0f / 130.0f); // ~39.75 frames
    for (int f = 0; f < 512; ++f) {
        float val = 0.0f;
        if (f % static_cast<int>(std::round(period_frames)) == 0) {
            val = 80.0f;
        }
        inducer.feed_sample(val);
    }

    TempoEstimate est = inducer.estimate_tempo();
    assert(est.bpm >= 128.5f && est.bpm <= 131.5f);
}

void test_insufficient_frames() {
    TempoInducer inducer;
    inducer.reset(86.133f);

    // Feed fewer than K_MAX_LAG * 2 (178 frames)
    for (int f = 0; f < 100; ++f) {
        inducer.feed_sample(50.0f);
    }

    TempoEstimate est = inducer.estimate_tempo();
    assert(est.bpm == 120.0f);
    assert(est.confidence == 0);
    assert(est.lag_frames == 43.0f);
}

void test_reset_clears_state() {
    TempoInducer inducer;
    inducer.reset(86.133f);

    for (int f = 0; f < 512; ++f) {
        inducer.feed_sample(100.0f);
    }
    TempoEstimate est_before = inducer.estimate_tempo();
    assert(est_before.bpm > 0.0f);
    assert(est_before.confidence > 0);

    inducer.reset(86.133f);
    TempoEstimate est_after = inducer.estimate_tempo();
    assert(est_after.bpm == 120.0f);
    assert(est_after.confidence == 0);
    assert(est_after.lag_frames == 43.0f);
}

int main() {
    test_tempo_induction_120bpm();
    test_octave_disambiguation();
    test_insufficient_frames();
    test_reset_clears_state();
    std::cout << "test_tempo_inducer PASSED\n";
    return 0;
}
