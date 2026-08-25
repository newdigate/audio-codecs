#include "src/flac/encoder/lpc_analyzer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace audio_codecs::flac {

void LpcAnalyzer::compute_lpc_coefficients(const int32_t* samples, 
                                           size_t count, 
                                           int max_order, 
                                           int& best_order, 
                                           int32_t* qlp_coeff, 
                                           int& qlp_shift, 
                                           int precision_bits) {
    best_order = 0;
    qlp_shift = 0;
    if (!samples || !qlp_coeff || count <= static_cast<size_t>(max_order) || max_order <= 0) {
        return;
    }

    if (max_order > 32) max_order = 32;
    if (precision_bits < 4) precision_bits = 4;
    if (precision_bits > 15) precision_bits = 15;

    // Windowed signal buffer (Hann window)
    double autoc[33] = {0.0};
    
    // Autocorrelation computation
    for (int lag = 0; lag <= max_order; ++lag) {
        double sum = 0.0;
        for (size_t i = lag; i < count; ++i) {
            double w1 = 0.5 * (1.0 - std::cos(2.0 * 3.141592653589793 * i / count));
            double w2 = 0.5 * (1.0 - std::cos(2.0 * 3.141592653589793 * (i - lag) / count));
            double s1 = samples[i] * w1;
            double s2 = samples[i - lag] * w2;
            sum += s1 * s2;
        }
        autoc[lag] = sum;
    }

    if (autoc[0] <= 1e-12) {
        return;
    }

    // Levinson-Durbin recursion
    double a[33] = {0.0};
    double a_next[33] = {0.0};
    double E = autoc[0];

    double best_coeff[33] = {0.0};
    double min_error = autoc[0];
    int opt_order = 0;

    for (int i = 1; i <= max_order; ++i) {
        double delta = autoc[i];
        for (int j = 1; j < i; ++j) {
            delta -= a[j] * autoc[i - j];
        }

        double k = delta / E;
        if (std::abs(k) >= 1.0) {
            break; // Unstable
        }

        a_next[i] = k;
        for (int j = 1; j < i; ++j) {
            a_next[j] = a[j] - k * a[i - j];
        }

        E *= (1.0 - k * k);

        for (int j = 1; j <= i; ++j) {
            a[j] = a_next[j];
        }

        if (E < min_error) {
            min_error = E;
            opt_order = i;
            for (int j = 1; j <= i; ++j) {
                best_coeff[j] = a[j];
            }
        }
    }

    if (opt_order <= 0) {
        return;
    }

    best_order = opt_order;

    // Fixed-point quantization with optimal shift
    double max_c = 0.0;
    for (int i = 1; i <= best_order; ++i) {
        max_c = std::max(max_c, std::abs(best_coeff[i]));
    }

    int max_qlp_val = (1 << (precision_bits - 1)) - 1;
    int shift = 0;
    if (max_c > 0.0) {
        // Find maximum shift q such that max_c * 2^q <= max_qlp_val
        int q = static_cast<int>(std::floor(std::log2(max_qlp_val / max_c)));
        if (q < 0) q = 0;
        if (q > 31) q = 31;
        shift = q;
    }

    qlp_shift = shift;
    double scale = static_cast<double>(1u << shift);

    for (int i = 0; i < best_order; ++i) {
        double scaled = best_coeff[i + 1] * scale;
        int32_t val = static_cast<int32_t>(std::round(scaled));
        if (val > max_qlp_val) val = max_qlp_val;
        if (val < -max_qlp_val - 1) val = -max_qlp_val - 1;
        qlp_coeff[i] = val;
    }
}

} // namespace audio_codecs::flac
