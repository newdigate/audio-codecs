#include "audio_codecs/spectrum/spectrum_filterbank.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace audio_codecs::spectrum;

int main() {
    SpectrumFilterbank fb;
    assert(fb.init(44100, 64, 20, 20000));
    assert(fb.num_bands() == 64);
    assert(fb.is_initialized());

    // Zero magnitudes produce zero dB band energy
    float zero_mags[512] = {0};
    uint8_t zero_bands[64] = {0};
    fb.compute_bands(zero_mags, zero_bands);
    for (size_t b = 0; b < 64; ++b) {
        assert(zero_bands[b] == 0);
    }

    // High energy in bin 10 (~440 Hz) produces peak around lower-mid band (~band 20-30)
    float test_mags[512] = {0};
    test_mags[10] = 30000.0f;
    uint8_t test_bands[64] = {0};
    fb.compute_bands(test_mags, test_bands);

    size_t peak_band = 0;
    uint8_t peak_val = 0;
    for (size_t b = 0; b < 64; ++b) {
        if (test_bands[b] > peak_val) {
            peak_val = test_bands[b];
            peak_band = b;
        }
    }
    assert(peak_val > 150);
    assert(peak_band >= 20 && peak_band <= 35);

    // Test Hann windowing
    int16_t in_pcm[1024];
    int16_t out_pcm_hann[1024] = {0};
    int16_t out_pcm_alias[1024] = {0};
    for (size_t i = 0; i < 1024; ++i) {
        in_pcm[i] = 10000;
    }
    fb.apply_hann_window(in_pcm, out_pcm_hann);
    fb.apply_window(in_pcm, out_pcm_alias);

    // Window boundaries: w(0) ~ 0, w(512) ~ max
    assert(out_pcm_hann[0] == 0);
    assert(std::abs(out_pcm_hann[512] - 10000) < 50);
    for (size_t i = 0; i < 1024; ++i) {
        assert(out_pcm_hann[i] == out_pcm_alias[i]);
    }

    // Custom band configuration
    SpectrumFilterbank custom_fb;
    assert(custom_fb.init(48000, 32, 50, 16000));
    assert(custom_fb.num_bands() == 32);

    // Invalid parameters rejected
    SpectrumFilterbank invalid_fb;
    assert(!invalid_fb.init(0, 64, 20, 20000));
    assert(!invalid_fb.init(44100, 0, 20, 20000));
    assert(!invalid_fb.init(44100, 64, 20000, 20));

    std::cout << "test_spectrum_filterbank PASSED (peak_band=" << peak_band << ", val=" << (int)peak_val << ")\n";
    return 0;
}
