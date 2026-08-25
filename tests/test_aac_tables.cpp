#include "src/aac/aac_tables.h"
#include "include/audio_codecs/aac/aac_types.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_sampling_rate_indices() {
    using namespace audio_codecs::aac;

    const uint32_t expected_rates[] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
    };

    for (int i = 0; i < 13; ++i) {
        assert(get_sampling_frequency_index(expected_rates[i]) == i);
        assert(get_sample_rate_from_index(i) == expected_rates[i]);
    }

    // Invalid rates
    assert(get_sampling_frequency_index(44101) == -1);
    assert(get_sampling_frequency_index(0) == -1);
    assert(get_sample_rate_from_index(-1) == 0);
    assert(get_sample_rate_from_index(13) == 0);
    assert(get_sample_rate_from_index(15) == 0);
}

void test_scalefactor_band_tables() {
    using namespace audio_codecs::aac;

    const uint32_t sample_rates[] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000
    };

    const size_t expected_long_bands[] = {
        41, 41, 47, 49, 49, 51,
        47, 47, 43, 43, 43, 40
    };

    const size_t expected_short_bands[] = {
        12, 12, 12, 14, 14, 14,
        15, 15, 15, 15, 15, 15
    };

    for (size_t i = 0; i < 12; ++i) {
        uint32_t sr = sample_rates[i];

        // Test Long SWB table
        size_t num_long = 0;
        const int* swb_long = get_swb_offset_long(sr, num_long);
        assert(swb_long != nullptr);
        assert(num_long == expected_long_bands[i]);
        assert(swb_long[0] == 0);
        assert(swb_long[num_long] == 1024);

        for (size_t b = 0; b < num_long; ++b) {
            assert(swb_long[b + 1] > swb_long[b]);
        }

        // Test Long SWB by index
        size_t num_long_idx = 0;
        const int* swb_long_idx = get_swb_offset_long_index(static_cast<int>(i), num_long_idx);
        assert(swb_long_idx == swb_long);
        assert(num_long_idx == num_long);

        // Test Short SWB table
        size_t num_short = 0;
        const int* swb_short = get_swb_offset_short(sr, num_short);
        assert(swb_short != nullptr);
        assert(num_short == expected_short_bands[i]);
        assert(swb_short[0] == 0);
        assert(swb_short[num_short] == 128);

        for (size_t b = 0; b < num_short; ++b) {
            assert(swb_short[b + 1] > swb_short[b]);
        }

        // Test Short SWB by index
        size_t num_short_idx = 0;
        const int* swb_short_idx = get_swb_offset_short_index(static_cast<int>(i), num_short_idx);
        assert(swb_short_idx == swb_short);
        assert(num_short_idx == num_short);
    }

    // Invalid queries
    size_t dummy_bands = 999;
    assert(get_swb_offset_long(12345, dummy_bands) == nullptr);
    assert(dummy_bands == 0);

    dummy_bands = 999;
    assert(get_swb_offset_short(12345, dummy_bands) == nullptr);
    assert(dummy_bands == 0);

    dummy_bands = 999;
    assert(get_swb_offset_long_index(99, dummy_bands) == nullptr);
    assert(dummy_bands == 0);

    dummy_bands = 999;
    assert(get_swb_offset_short_index(-1, dummy_bands) == nullptr);
    assert(dummy_bands == 0);
}

void test_windows() {
    using namespace audio_codecs::aac;

    // 1. Sine window 1024 (length 2048)
    const float* sine_1024 = get_sine_window_1024();
    assert(sine_1024 != nullptr);
    for (int i = 0; i < 1024; ++i) {
        // Symmetry: w[i] == w[2048 - 1 - i]
        assert(std::fabs(sine_1024[i] - sine_1024[2048 - 1 - i]) < 1e-6f);
        // Princen-Bradley condition: w[i]^2 + w[i + 1024]^2 == 1.0
        float pb = sine_1024[i] * sine_1024[i] + sine_1024[i + 1024] * sine_1024[i + 1024];
        assert(std::fabs(pb - 1.0f) < 1e-5f);
    }

    // 2. Sine window 128 (length 256)
    const float* sine_128 = get_sine_window_128();
    assert(sine_128 != nullptr);
    for (int i = 0; i < 128; ++i) {
        // Symmetry: w[i] == w[256 - 1 - i]
        assert(std::fabs(sine_128[i] - sine_128[256 - 1 - i]) < 1e-6f);
        // Princen-Bradley condition: w[i]^2 + w[i + 128]^2 == 1.0
        float pb = sine_128[i] * sine_128[i] + sine_128[i + 128] * sine_128[i + 128];
        assert(std::fabs(pb - 1.0f) < 1e-5f);
    }

    // 3. KBD window 1024 (length 2048)
    const float* kbd_1024 = get_kbd_window_1024();
    assert(kbd_1024 != nullptr);
    for (int i = 0; i < 1024; ++i) {
        // Symmetry: w[i] == w[2048 - 1 - i]
        assert(std::fabs(kbd_1024[i] - kbd_1024[2048 - 1 - i]) < 1e-6f);
        // Princen-Bradley condition: w[i]^2 + w[i + 1024]^2 == 1.0
        float pb = kbd_1024[i] * kbd_1024[i] + kbd_1024[i + 1024] * kbd_1024[i + 1024];
        assert(std::fabs(pb - 1.0f) < 1e-5f);
    }

    // 4. KBD window 128 (length 256)
    const float* kbd_128 = get_kbd_window_128();
    assert(kbd_128 != nullptr);
    for (int i = 0; i < 128; ++i) {
        // Symmetry: w[i] == w[256 - 1 - i]
        assert(std::fabs(kbd_128[i] - kbd_128[256 - 1 - i]) < 1e-6f);
        // Princen-Bradley condition: w[i]^2 + w[i + 128]^2 == 1.0
        float pb = kbd_128[i] * kbd_128[i] + kbd_128[i + 128] * kbd_128[i + 128];
        assert(std::fabs(pb - 1.0f) < 1e-5f);
    }
}

void test_composite_windows() {
    using namespace audio_codecs::aac;

    const WindowShape shapes[] = { WindowShape::Sine, WindowShape::KBD };

    for (auto shape_prev : shapes) {
        for (auto shape_curr : shapes) {
            // OnlyLong
            size_t len = 0;
            const float* w_long = get_window(WindowSequence::OnlyLong, shape_prev, shape_curr, len);
            assert(w_long != nullptr);
            assert(len == 2048);
            const float* expected_prev_long = (shape_prev == WindowShape::Sine) ? get_sine_window_1024() : get_kbd_window_1024();
            const float* expected_curr_long = (shape_curr == WindowShape::Sine) ? get_sine_window_1024() : get_kbd_window_1024();
            for (int i = 0; i < 1024; ++i) {
                assert(std::fabs(w_long[i] - expected_prev_long[i]) < 1e-6f);
                assert(std::fabs(w_long[1024 + i] - expected_curr_long[1024 + i]) < 1e-6f);
            }

            // EightShort
            const float* w_short = get_window(WindowSequence::EightShort, shape_prev, shape_curr, len);
            assert(w_short != nullptr);
            assert(len == 256);
            const float* expected_prev_short = (shape_prev == WindowShape::Sine) ? get_sine_window_128() : get_kbd_window_128();
            const float* expected_curr_short = (shape_curr == WindowShape::Sine) ? get_sine_window_128() : get_kbd_window_128();
            for (int i = 0; i < 128; ++i) {
                assert(std::fabs(w_short[i] - expected_prev_short[i]) < 1e-6f);
                assert(std::fabs(w_short[128 + i] - expected_curr_short[128 + i]) < 1e-6f);
            }

            // LongStart
            const float* w_start = get_window(WindowSequence::LongStart, shape_prev, shape_curr, len);
            assert(w_start != nullptr);
            assert(len == 2048);
            // 0..1023: shape_prev long
            for (int i = 0; i < 1024; ++i) {
                assert(std::fabs(w_start[i] - expected_prev_long[i]) < 1e-6f);
            }
            // 1024..1471: 1.0f
            for (int i = 1024; i < 1472; ++i) {
                assert(std::fabs(w_start[i] - 1.0f) < 1e-6f);
            }
            // 1472..1599: shape_curr short right half (128..255)
            for (int i = 0; i < 128; ++i) {
                assert(std::fabs(w_start[1472 + i] - expected_curr_short[128 + i]) < 1e-6f);
            }
            // 1600..2047: 0.0f
            for (int i = 1600; i < 2048; ++i) {
                assert(std::fabs(w_start[i] - 0.0f) < 1e-6f);
            }

            // LongStop
            const float* w_stop = get_window(WindowSequence::LongStop, shape_prev, shape_curr, len);
            assert(w_stop != nullptr);
            assert(len == 2048);
            // 0..447: 0.0f
            for (int i = 0; i < 448; ++i) {
                assert(std::fabs(w_stop[i] - 0.0f) < 1e-6f);
            }
            // 448..575: shape_prev short left half (0..127)
            for (int i = 0; i < 128; ++i) {
                assert(std::fabs(w_stop[448 + i] - expected_prev_short[i]) < 1e-6f);
            }
            // 576..1023: 1.0f
            for (int i = 576; i < 1024; ++i) {
                assert(std::fabs(w_stop[i] - 1.0f) < 1e-6f);
            }
            // 1024..2047: shape_curr long right half (1024..2047)
            for (int i = 0; i < 1024; ++i) {
                assert(std::fabs(w_stop[1024 + i] - expected_curr_long[1024 + i]) < 1e-6f);
            }
        }
    }
}

void test_dequant_pow43() {
    using namespace audio_codecs::aac;

    // Zero and unity
    assert(std::fabs(dequant_pow43(0) - 0.0f) < 1e-5f);
    assert(std::fabs(dequant_pow43(1) - 1.0f) < 1e-5f);
    assert(std::fabs(dequant_pow43(8) - 16.0f) < 1e-4f);
    assert(std::fabs(dequant_pow43(27) - 81.0f) < 1e-4f);
    assert(std::fabs(dequant_pow43(64) - 256.0f) < 1e-4f);

    // Range 0..256
    for (int i = 0; i <= 256; ++i) {
        float expected = std::pow(static_cast<float>(i), 4.0f / 3.0f);
        float actual = dequant_pow43(i);
        assert(std::fabs(actual - expected) < 1e-4f);
    }

    // Negative values
    assert(std::fabs(dequant_pow43(-8) - 16.0f) < 1e-4f);
    assert(std::fabs(dequant_pow43(-27) - 81.0f) < 1e-4f);

    // Values > 256
    int test_large[] = {257, 512, 1000, 4096, 8192};
    for (int val : test_large) {
        float expected = std::pow(static_cast<float>(val), 4.0f / 3.0f);
        float actual = dequant_pow43(val);
        assert(std::fabs(actual - expected) < 1e-3f);
    }
}

int main() {
    std::cout << "Testing AAC Tables...\n";
    test_sampling_rate_indices();
    test_scalefactor_band_tables();
    test_windows();
    test_composite_windows();
    test_dequant_pow43();
    std::cout << "AAC Tables test passed!\n";
    return 0;
}
