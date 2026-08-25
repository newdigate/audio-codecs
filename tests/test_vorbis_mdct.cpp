#include "src/vorbis/vorbis_mdct.h"
#include "src/vorbis/vorbis_common.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;
    const size_t N = 512;
    VorbisMdct mdct;
    assert(mdct.init(N));

    std::vector<float> win(N);
    vorbis_generate_window(win.data(), N);

    // Overlap-add test across two consecutive 50% overlapping blocks
    std::vector<float> input_signal(N + N / 2);
    for (size_t i = 0; i < input_signal.size(); ++i) {
        input_signal[i] = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
    }

    std::vector<float> b1_in(N), b2_in(N);
    for (size_t i = 0; i < N; ++i) {
        b1_in[i] = input_signal[i] * win[i];
        b2_in[i] = input_signal[i + N / 2] * win[i];
    }

    std::vector<float> b1_freq(N / 2), b2_freq(N / 2);
    mdct.forward_mdct(b1_in.data(), b1_freq.data());
    mdct.forward_mdct(b2_in.data(), b2_freq.data());

    std::vector<float> b1_out(N), b2_out(N);
    mdct.inverse_imdct(b1_freq.data(), b1_out.data());
    mdct.inverse_imdct(b2_freq.data(), b2_out.data());

    // Window again at output
    for (size_t i = 0; i < N; ++i) {
        b1_out[i] *= win[i];
        b2_out[i] *= win[i];
    }

    // Check overlap region (second half of b1 + first half of b2) against original input
    float max_diff = 0.0f;
    size_t max_i = 0;
    for (size_t i = 0; i < N / 2; ++i) {
        float reconstructed = b1_out[i + N / 2] + b2_out[i];
        float expected = input_signal[i + N / 2];
        float diff = std::fabs(reconstructed - expected);
        if (diff > max_diff) {
            max_diff = diff;
            max_i = i;
        }
    }
    std::cout << "max_diff=" << max_diff << " at i=" << max_i << "\n";
    assert(max_diff < 1e-3f);

    std::cout << "Vorbis MDCT/IMDCT invertibility and windowing test passed!\n";
    return 0;
}
