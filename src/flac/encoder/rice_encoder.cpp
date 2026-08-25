#include "src/flac/encoder/rice_encoder.h"
#include <algorithm>
#include <cmath>

namespace audio_codecs::flac {

uint8_t RiceEncoder::find_optimal_rice_param(const int32_t* residual, size_t count, uint8_t rice_param_bits) {
    if (!residual || count == 0) return 0;

    uint64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += fold(residual[i]);
    }

    uint8_t max_param = (rice_param_bits == 4) ? 14 : 30; // escape is 15 or 31

    if (sum == 0) return 0;

    double mean = static_cast<double>(sum) / count;
    // Optimal k ~= log2(mean * ln(2))
    int est_k = 0;
    if (mean > 0.0) {
        est_k = static_cast<int>(std::floor(std::log2(mean * 0.6931471805599453)));
    }
    if (est_k < 0) est_k = 0;
    if (est_k > max_param) est_k = max_param;

    // Search around estimated k for minimum total bits
    uint64_t best_bits = UINT64_MAX;
    uint8_t best_param = static_cast<uint8_t>(est_k);

    int start_k = std::max(0, est_k - 1);
    int end_k = std::min(static_cast<int>(max_param), est_k + 1);

    for (int k = start_k; k <= end_k; ++k) {
        uint64_t total_bits = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t u = fold(residual[i]);
            uint32_t msbs = u >> k;
            total_bits += (msbs + 1) + k;
        }
        if (total_bits < best_bits) {
            best_bits = total_bits;
            best_param = static_cast<uint8_t>(k);
        }
    }

    return best_param;
}

void RiceEncoder::encode_residual_partition(core::BitWriter& writer, 
                                            const int32_t* residual, 
                                            size_t count, 
                                            uint8_t rice_param_bits, 
                                            uint8_t param) {
    if (!residual) return;

    // Write partition parameter
    writer.write_bits(param, rice_param_bits);

    for (size_t i = 0; i < count; ++i) {
        uint32_t u = fold(residual[i]);
        uint32_t msbs = u >> param;
        uint32_t lsbs = u & ((1u << param) - 1);

        // Write msbs zeros followed by a 1
        for (uint32_t z = 0; z < msbs; ++z) {
            writer.write_bits(0, 1);
        }
        writer.write_bits(1, 1);

        if (param > 0) {
            writer.write_bits(lsbs, param);
        }
    }
}

void RiceEncoder::encode_escaped_partition(core::BitWriter& writer, 
                                          const int32_t* residual, 
                                          size_t count, 
                                          uint8_t bit_depth) {
    if (!residual) return;

    // Escape parameter: 0b1111 (4-bit)
    writer.write_bits(0x0F, 4);
    writer.write_bits(bit_depth, 5);

    for (size_t i = 0; i < count; ++i) {
        writer.write_bits(static_cast<uint32_t>(residual[i]), bit_depth);
    }
}

} // namespace audio_codecs::flac
