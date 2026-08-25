// tests/test_flac_lpc.cpp
#include "src/flac/encoder/lpc_analyzer.h"
#include "src/flac/decoder/subframe_decoder.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    // Generate sinusoidal sequence
    int32_t samples[256];
    for (int i = 0; i < 256; ++i) {
        samples[i] = static_cast<int32_t>(10000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / 44100.0));
    }

    int best_order = 0;
    int32_t qlp_coeff[32] = {0};
    int qlp_shift = 0;

    LpcAnalyzer::compute_lpc_coefficients(samples, 256, 8, best_order, qlp_coeff, qlp_shift, 14);

    assert(best_order >= 2);
    assert(qlp_shift >= 0);

    // Compute LPC residual
    int32_t residual[256];
    for (int i = 0; i < best_order; ++i) {
        residual[i] = samples[i];
    }
    for (int i = best_order; i < 256; ++i) {
        int64_t sum = 0;
        for (int j = 0; j < best_order; ++j) {
            sum += static_cast<int64_t>(qlp_coeff[j]) * samples[i - 1 - j];
        }
        residual[i] = samples[i] - static_cast<int32_t>(sum >> qlp_shift);
    }

    // Reconstruct via LpcPredictor
    int32_t restored[256] = {0};
    for (int i = 0; i < best_order; ++i) {
        restored[i] = samples[i];
    }
    LpcPredictor::restore_samples(residual, 256, best_order, qlp_coeff, qlp_shift, restored);

    for (int i = 0; i < 256; ++i) {
        assert(restored[i] == samples[i]);
    }

    std::cout << "FLAC LPC analyzer test passed! (Order: " << best_order << ", Shift: " << qlp_shift << ")\n";
    return 0;
}
