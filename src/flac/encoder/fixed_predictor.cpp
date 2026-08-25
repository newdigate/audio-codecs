#include "src/flac/encoder/fixed_predictor.h"
#include <cmath>
#include <cstdlib>

namespace audio_codecs::flac {

void FixedPredictor::compute_residual(const int32_t* samples, size_t count, int order, int32_t* out_residual) {
    if (!samples || !out_residual || count == 0) return;

    // Warm-up samples copied verbatim
    for (int i = 0; i < order && i < static_cast<int>(count); ++i) {
        out_residual[i] = samples[i];
    }

    switch (order) {
        case 0:
            for (size_t i = 0; i < count; ++i) {
                out_residual[i] = samples[i];
            }
            break;
        case 1:
            for (size_t i = 1; i < count; ++i) {
                out_residual[i] = samples[i] - samples[i - 1];
            }
            break;
        case 2:
            for (size_t i = 2; i < count; ++i) {
                out_residual[i] = samples[i] - 2 * samples[i - 1] + samples[i - 2];
            }
            break;
        case 3:
            for (size_t i = 3; i < count; ++i) {
                out_residual[i] = samples[i] - 3 * samples[i - 1] + 3 * samples[i - 2] - samples[i - 3];
            }
            break;
        case 4:
            for (size_t i = 4; i < count; ++i) {
                out_residual[i] = samples[i] - 4 * samples[i - 1] + 6 * samples[i - 2] - 4 * samples[i - 3] + samples[i - 4];
            }
            break;
        default:
            break;
    }
}

void FixedPredictor::restore_samples(const int32_t* residual, size_t count, int order, int32_t* inout_samples) {
    if (!residual || !inout_samples || count == 0) return;

    switch (order) {
        case 0:
            for (size_t i = 0; i < count; ++i) {
                inout_samples[i] = residual[i];
            }
            break;
        case 1:
            for (size_t i = 1; i < count; ++i) {
                inout_samples[i] = residual[i] + inout_samples[i - 1];
            }
            break;
        case 2:
            for (size_t i = 2; i < count; ++i) {
                inout_samples[i] = residual[i] + 2 * inout_samples[i - 1] - inout_samples[i - 2];
            }
            break;
        case 3:
            for (size_t i = 3; i < count; ++i) {
                inout_samples[i] = residual[i] + 3 * inout_samples[i - 1] - 3 * inout_samples[i - 2] + inout_samples[i - 3];
            }
            break;
        case 4:
            for (size_t i = 4; i < count; ++i) {
                inout_samples[i] = residual[i] + 4 * inout_samples[i - 1] - 6 * inout_samples[i - 2] + 4 * inout_samples[i - 3] - inout_samples[i - 4];
            }
            break;
        default:
            break;
    }
}

int FixedPredictor::find_best_fixed_order(const int32_t* samples, size_t count) {
    if (!samples || count <= 4) return 0;

    uint64_t min_abs_sum = UINT64_MAX;
    int best_order = 0;

    for (int order = 0; order <= 4; ++order) {
        uint64_t abs_sum = 0;
        for (size_t i = order; i < count; ++i) {
            int32_t res = 0;
            switch (order) {
                case 0: res = samples[i]; break;
                case 1: res = samples[i] - samples[i - 1]; break;
                case 2: res = samples[i] - 2 * samples[i - 1] + samples[i - 2]; break;
                case 3: res = samples[i] - 3 * samples[i - 1] + 3 * samples[i - 2] - samples[i - 3]; break;
                case 4: res = samples[i] - 4 * samples[i - 1] + 6 * samples[i - 2] - 4 * samples[i - 3] + samples[i - 4]; break;
            }
            abs_sum += std::abs(res);
        }
        if (abs_sum < min_abs_sum) {
            min_abs_sum = abs_sum;
            best_order = order;
        }
    }

    return best_order;
}

} // namespace audio_codecs::flac
