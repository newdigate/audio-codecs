#include "src/aac/aac_mdct.h"
#include "src/aac/aac_tables.h"
#include "include/audio_codecs/aac/aac_types.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;

void test_tdac_long_sine() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test Time-Domain Alias Cancellation (TDAC) on 2 consecutive frames
    // Frame 0: samples 0..2047
    // Frame 1: samples 1024..3071
    std::vector<float> original_signal(3072);
    for (size_t i = 0; i < original_signal.size(); ++i) {
        original_signal[i] = static_cast<float>(std::sin(2.0 * PI * 440.0 * i / 44100.0));
    }

    float freq0[1024] = {0.0f};
    float freq1[1024] = {0.0f};
    mdct.forward_long(&original_signal[0], freq0, WindowShape::Sine);
    mdct.forward_long(&original_signal[1024], freq1, WindowShape::Sine);

    float time0[2048] = {0.0f};
    float time1[2048] = {0.0f};
    mdct.inverse_long(freq0, time0, WindowShape::Sine);
    mdct.inverse_long(freq1, time1, WindowShape::Sine);

    // Overlap-add second half of frame 0 with first half of frame 1:
    // time0[1024..2047] + time1[0..1023] should equal original_signal[1024..2047]
    float max_err = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        float reconstructed = time0[1024 + i] + time1[i];
        float orig = original_signal[1024 + i];
        float err = std::fabs(reconstructed - orig);
        if (err > max_err) max_err = err;
        assert(err < 1e-4f);
    }
    std::cout << "Long Sine Window TDAC max error: " << max_err << "\n";
}

void test_tdac_long_kbd() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test TDAC with KBD window on a multi-frequency signal
    std::vector<float> original_signal(3072);
    for (size_t i = 0; i < original_signal.size(); ++i) {
        original_signal[i] = static_cast<float>(
            0.6 * std::sin(2.0 * PI * 220.0 * i / 44100.0) +
            0.3 * std::sin(2.0 * PI * 1760.0 * i / 44100.0) +
            0.1 * std::cos(2.0 * PI * 8800.0 * i / 44100.0)
        );
    }

    float freq0[1024] = {0.0f};
    float freq1[1024] = {0.0f};
    mdct.forward_long(&original_signal[0], freq0, WindowShape::KBD);
    mdct.forward_long(&original_signal[1024], freq1, WindowShape::KBD);

    float time0[2048] = {0.0f};
    float time1[2048] = {0.0f};
    mdct.inverse_long(freq0, time0, WindowShape::KBD);
    mdct.inverse_long(freq1, time1, WindowShape::KBD);

    float max_err = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        float reconstructed = time0[1024 + i] + time1[i];
        float orig = original_signal[1024 + i];
        float err = std::fabs(reconstructed - orig);
        if (err > max_err) max_err = err;
        assert(err < 1e-4f);
    }
    std::cout << "Long KBD Window TDAC max error: " << max_err << "\n";
}

void test_tdac_short_sine_and_kbd() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test single short block (256 samples in, 128 freq out) TDAC
    for (auto shape : {WindowShape::Sine, WindowShape::KBD}) {
        std::vector<float> signal(384);
        for (size_t i = 0; i < signal.size(); ++i) {
            signal[i] = static_cast<float>(std::sin(2.0 * PI * 1000.0 * i / 44100.0));
        }

        float freq0[128] = {0.0f};
        float freq1[128] = {0.0f};
        mdct.forward_short(&signal[0], freq0, shape);
        mdct.forward_short(&signal[128], freq1, shape);

        float time0[256] = {0.0f};
        float time1[256] = {0.0f};
        mdct.inverse_short(freq0, time0, shape);
        mdct.inverse_short(freq1, time1, shape);

        float max_err = 0.0f;
        for (int i = 0; i < 128; ++i) {
            float reconstructed = time0[128 + i] + time1[i];
            float orig = signal[128 + i];
            float err = std::fabs(reconstructed - orig);
            if (err > max_err) max_err = err;
            assert(err < 1e-4f);
        }
        std::cout << "Single Short Window TDAC (" << (shape == WindowShape::Sine ? "Sine" : "KBD")
                  << ") max error: " << max_err << "\n";
    }
}

void test_tdac_eight_short() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test EightShort window frame reconstruction
    // Frame 0: 0..2047, Frame 1: 1024..3071
    std::vector<float> original_signal(3072);
    for (size_t i = 0; i < original_signal.size(); ++i) {
        original_signal[i] = static_cast<float>(
            0.5 * std::sin(2.0 * PI * 500.0 * i / 44100.0) +
            0.5 * std::sin(2.0 * PI * 3500.0 * i / 44100.0)
        );
    }

    float freq0[1024] = {0.0f};
    float freq1[1024] = {0.0f};
    mdct.forward_eight_short(&original_signal[0], freq0, WindowShape::Sine);
    mdct.forward_eight_short(&original_signal[1024], freq1, WindowShape::Sine);

    float time0[2048] = {0.0f};
    float time1[2048] = {0.0f};
    mdct.inverse_eight_short(freq0, time0, WindowShape::Sine);
    mdct.inverse_eight_short(freq1, time1, WindowShape::Sine);

    float max_err = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        float reconstructed = time0[1024 + i] + time1[i];
        float orig = original_signal[1024 + i];
        float err = std::fabs(reconstructed - orig);
        if (err > max_err) max_err = err;
        assert(err < 1e-4f);
    }
    std::cout << "EightShort Frame TDAC max error: " << max_err << "\n";
}

void test_window_sequence_transitions() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test transition sequence:
    // Frame 0: OnlyLong
    // Frame 1: LongStart
    // Frame 2: EightShort
    // Frame 3: LongStop
    // Frame 4: OnlyLong
    const size_t total_samples = 1024 * 6; // 6144 samples
    std::vector<float> signal(total_samples);
    for (size_t i = 0; i < total_samples; ++i) {
        signal[i] = static_cast<float>(
            0.4 * std::sin(2.0 * PI * 300.0 * i / 44100.0) +
            0.4 * std::cos(2.0 * PI * 2400.0 * i / 44100.0) +
            0.2 * std::sin(2.0 * PI * 12000.0 * i / 44100.0)
        );
    }

    struct FrameConfig {
        WindowSequence seq;
        WindowShape prev_shape;
        WindowShape curr_shape;
    };

    std::vector<FrameConfig> sequence = {
        { WindowSequence::OnlyLong,   WindowShape::Sine, WindowShape::Sine },
        { WindowSequence::LongStart,  WindowShape::Sine, WindowShape::Sine },
        { WindowSequence::EightShort, WindowShape::Sine, WindowShape::Sine },
        { WindowSequence::LongStop,   WindowShape::Sine, WindowShape::Sine },
        { WindowSequence::OnlyLong,   WindowShape::Sine, WindowShape::Sine }
    };

    std::vector<std::vector<float>> freq_frames(sequence.size(), std::vector<float>(1024));
    std::vector<std::vector<float>> time_frames(sequence.size(), std::vector<float>(2048));

    // Forward transform
    for (size_t f = 0; f < sequence.size(); ++f) {
        const float* in_ptr = &signal[f * 1024];
        mdct.forward_windowed(in_ptr, freq_frames[f].data(),
                              sequence[f].seq, sequence[f].prev_shape, sequence[f].curr_shape);
    }

    // Inverse transform
    for (size_t f = 0; f < sequence.size(); ++f) {
        mdct.inverse_windowed(freq_frames[f].data(), time_frames[f].data(),
                              sequence[f].seq, sequence[f].prev_shape, sequence[f].curr_shape);
    }

    // Overlap-add between adjacent frames f and f+1
    for (size_t f = 0; f < sequence.size() - 1; ++f) {
        float max_err = 0.0f;
        for (int i = 0; i < 1024; ++i) {
            float reconstructed = time_frames[f][1024 + i] + time_frames[f + 1][i];
            float orig = signal[(f + 1) * 1024 + i];
            float err = std::fabs(reconstructed - orig);
            if (err > max_err) max_err = err;
            assert(err < 1e-4f);
        }
        std::cout << "Transition " << f << " -> " << (f + 1) << " TDAC max error: " << max_err << "\n";
    }
}

void test_noise_tdac() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test TDAC on white noise and pulse inputs
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<float> noise(3072);
    for (size_t i = 0; i < noise.size(); ++i) {
        noise[i] = dist(rng);
    }

    float freq0[1024] = {0.0f};
    float freq1[1024] = {0.0f};
    mdct.forward_long(&noise[0], freq0, WindowShape::Sine);
    mdct.forward_long(&noise[1024], freq1, WindowShape::Sine);

    float time0[2048] = {0.0f};
    float time1[2048] = {0.0f};
    mdct.inverse_long(freq0, time0, WindowShape::Sine);
    mdct.inverse_long(freq1, time1, WindowShape::Sine);

    float max_err = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        float reconstructed = time0[1024 + i] + time1[i];
        float orig = noise[1024 + i];
        float err = std::fabs(reconstructed - orig);
        if (err > max_err) max_err = err;
        assert(err < 1e-4f);
    }
    std::cout << "White Noise TDAC max error: " << max_err << "\n";
}

} // anonymous namespace

int main() {
    std::cout << "Running AAC MDCT tests...\n";
    test_tdac_long_sine();
    test_tdac_long_kbd();
    test_tdac_short_sine_and_kbd();
    test_tdac_eight_short();
    test_window_sequence_transitions();
    test_noise_tdac();
    std::cout << "All AAC MDCT tests passed successfully!\n";
    return 0;
}
