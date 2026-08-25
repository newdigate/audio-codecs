// tests/test_flac_predictors.cpp
#include "src/flac/encoder/fixed_predictor.h"
#include "src/flac/decoder/subframe_decoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    // Test signal
    int32_t samples[32] = {
        100, 105, 112, 120, 131, 145, 160, 178,
        195, 210, 222, 230, 235, 236, 232, 225,
        215, 201, 185, 166, 145, 122, 100, 78,
        58, 40, 25, 12, 4, 0, 1, 6
    };

    int32_t orig[32];
    for (int i = 0; i < 32; ++i) orig[i] = samples[i];

    // Test Fixed Predictor Orders 0..4
    for (int order = 0; order <= 4; ++order) {
        int32_t residual[32] = {0};
        FixedPredictor::compute_residual(orig, 32, order, residual);

        int32_t restored[32] = {0};
        for (int i = 0; i < order; ++i) restored[i] = orig[i]; // warmups
        FixedPredictor::restore_samples(residual, 32, order, restored);

        for (int i = 0; i < 32; ++i) {
            assert(restored[i] == orig[i]);
        }
    }

    // Test LPC Predictor with handcrafted coefficients
    int lpc_order = 3;
    int32_t qlp_coeff[3] = {1200, -500, 150};
    int qlp_shift = 10; // division by 1024

    int32_t lpc_residual[32] = {0};
    // Compute residual using LPC formula
    for (int i = 0; i < lpc_order; ++i) {
        lpc_residual[i] = orig[i];
    }
    for (int i = lpc_order; i < 32; ++i) {
        int64_t sum = 0;
        for (int j = 0; j < lpc_order; ++j) {
            sum += static_cast<int64_t>(qlp_coeff[j]) * orig[i - 1 - j];
        }
        int32_t pred = static_cast<int32_t>(sum >> qlp_shift);
        lpc_residual[i] = orig[i] - pred;
    }

    int32_t lpc_restored[32] = {0};
    for (int i = 0; i < lpc_order; ++i) lpc_restored[i] = orig[i];
    LpcPredictor::restore_samples(lpc_residual, 32, lpc_order, qlp_coeff, qlp_shift, lpc_restored);

    for (int i = 0; i < 32; ++i) {
        assert(lpc_restored[i] == orig[i]);
    }

    std::cout << "FLAC Predictor tests passed!\n";
    return 0;
}
