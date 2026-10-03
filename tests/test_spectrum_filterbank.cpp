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

    // 1. Zero magnitudes produce zero dB band energy
    float zero_mags[512] = {0};
    uint8_t zero_bands[64] = {0};
    fb.compute_bands(zero_mags, zero_bands);
    for (size_t b = 0; b < 64; ++b) {
        assert(zero_bands[b] == 0);
    }

    // 2. High energy in bin 10 (~440 Hz) with full-scale reference normalization and gradation
    constexpr float kFullScalePeak = 32768.0f * 512.0f; // Full scale FFT peak magnitude

    // Full-scale tone at bin 10
    float fs_mags[512] = {0};
    fs_mags[10] = kFullScalePeak;
    uint8_t fs_bands[64] = {0};
    fb.compute_bands(fs_mags, fs_bands);

    size_t peak_band = 0;
    uint8_t fs_peak_val = 0;
    for (size_t b = 0; b < 64; ++b) {
        if (fs_bands[b] > fs_peak_val) {
            fs_peak_val = fs_bands[b];
            peak_band = b;
        }
    }
    assert(fs_peak_val >= 250);
    assert(peak_band >= 20 && peak_band <= 35);

    // Intermediate energy tone (-20 dBFS, magnitude = 0.1 * full scale)
    float mid_mags[512] = {0};
    mid_mags[10] = kFullScalePeak * 0.1f;
    uint8_t mid_bands[64] = {0};
    fb.compute_bands(mid_mags, mid_bands);
    uint8_t mid_peak_val = mid_bands[peak_band];

    // Low energy tone (-40 dBFS, magnitude = 0.01 * full scale)
    float low_mags[512] = {0};
    low_mags[10] = kFullScalePeak * 0.01f;
    uint8_t low_bands[64] = {0};
    fb.compute_bands(low_mags, low_bands);
    uint8_t low_peak_val = low_bands[peak_band];

    // Verify gradation and expected dynamic range mapping
    assert(mid_peak_val > 180 && mid_peak_val < 220); // ~202 for -20 dBFS
    assert(low_peak_val > 130 && low_peak_val < 165); // ~149 for -40 dBFS
    assert(low_peak_val < mid_peak_val);
    assert(mid_peak_val < fs_peak_val);

    // 3. Test Hann windowing
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

    // 4. Nyquist clamping and bin index safety [1, 511]
    SpectrumFilterbank nyquist_fb;
    assert(nyquist_fb.init(16000, 64, 20, 20000)); // max_freq clamped to 8000 Hz
    for (size_t b = 0; b < nyquist_fb.num_bands(); ++b) {
        assert(nyquist_fb.band_start_bin(b) >= 1);
        assert(nyquist_fb.band_end_bin(b) <= 511);
        assert(nyquist_fb.band_start_bin(b) <= nyquist_fb.band_end_bin(b));
    }

    SpectrumFilterbank overnyquist_fb;
    assert(overnyquist_fb.init(44100, 64, 20, 30000)); // max_freq clamped to 22050 Hz
    for (size_t b = 0; b < overnyquist_fb.num_bands(); ++b) {
        assert(overnyquist_fb.band_start_bin(b) >= 1);
        assert(overnyquist_fb.band_end_bin(b) <= 511);
        assert(overnyquist_fb.band_start_bin(b) <= overnyquist_fb.band_end_bin(b));
    }

    SpectrumFilterbank highres_fb;
    assert(highres_fb.init(192000, 64, 20, 20000)); // Nyquist = 96000 Hz, does not overflow uint16
    assert(highres_fb.is_initialized());
    for (size_t b = 0; b < highres_fb.num_bands(); ++b) {
        assert(highres_fb.band_start_bin(b) >= 1);
        assert(highres_fb.band_end_bin(b) <= 511);
        assert(highres_fb.band_start_bin(b) <= highres_fb.band_end_bin(b));
    }

    // 5. Invalid parameters rejected and initialized_ set to false
    SpectrumFilterbank invalid_fb;
    assert(!invalid_fb.init(0, 64, 20, 20000));
    assert(!invalid_fb.is_initialized());

    assert(!invalid_fb.init(44100, 0, 20, 20000));
    assert(!invalid_fb.is_initialized());

    assert(!invalid_fb.init(44100, 64, 20000, 20));
    assert(!invalid_fb.is_initialized());

    // Nyquist (4000) <= min_freq (5000)
    assert(!invalid_fb.init(8000, 64, 5000, 20000));
    assert(!invalid_fb.is_initialized());

    std::cout << "test_spectrum_filterbank PASSED (peak_band=" << peak_band
              << ", fs=" << (int)fs_peak_val
              << ", -20dB=" << (int)mid_peak_val
              << ", -40dB=" << (int)low_peak_val << ")\n";
    return 0;
}
