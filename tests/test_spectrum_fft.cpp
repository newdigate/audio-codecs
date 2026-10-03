#include "audio_codecs/spectrum/desktop_fft.h"
#include "audio_codecs/spectrum/teensy_fft_adapter.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

using namespace audio_codecs::spectrum;

struct MockTeensyFft {
    bool called{false};
    void forward_1024(const int16_t* /*in_pcm*/, float* out_magnitudes_512) {
        called = true;
        out_magnitudes_512[10] = 1234.0f;
    }
};

int main() {
    DesktopRealFftBackend fft;
    assert(fft.init());

    // Generate 1024 samples of a 440 Hz sine wave at 44100 Hz sample rate
    // Expected peak bin: round(440 * 1024 / 44100) = round(10.22) = 10
    std::vector<int16_t> pcm(1024);
    for (size_t i = 0; i < 1024; ++i) {
        float t = static_cast<float>(i) / 44100.0f;
        pcm[i] = static_cast<int16_t>(std::sin(2.0f * 3.14159265f * 440.0f * t) * 30000.0f);
    }

    float mags[512] = {0};
    fft.forward_1024(pcm.data(), mags);

    size_t peak_bin = 0;
    float peak_mag = 0.0f;
    for (size_t i = 1; i < 512; ++i) {
        if (mags[i] > peak_mag) {
            peak_mag = mags[i];
            peak_bin = i;
        }
    }

    assert(peak_bin == 10);
    assert(peak_mag > 1000.0f);

    // Test TeensyAudioFftAdapter template
    MockTeensyFft mock;
    TeensyAudioFftAdapter<MockTeensyFft> adapter(mock);
    FftBackend& backend_ref = adapter;
    float mock_mags[512] = {0};
    backend_ref.forward_1024(pcm.data(), mock_mags);
    assert(mock.called);
    assert(mock_mags[10] == 1234.0f);

    std::cout << "test_spectrum_fft PASSED (peak_bin=" << peak_bin << ")\n";
    return 0;
}
