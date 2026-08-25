#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class FixedPredictor {
public:
    // Compute residual for orders 0..4 (RFC 9639 Section 8.3.3)
    static void compute_residual(const int32_t* samples, size_t count, int order, int32_t* out_residual);

    // Restore samples from residual for orders 0..4
    static void restore_samples(const int32_t* residual, size_t count, int order, int32_t* inout_samples);

    // Evaluate residual bit estimation / energy to choose best fixed order 0..4
    static int find_best_fixed_order(const int32_t* samples, size_t count);
};

} // namespace audio_codecs::flac
