#include "src/aac/decoder/tns_decoder.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio_codecs::aac {

void decode_tns_coef(int order, int coef_res, const int* raw_coef, float* lpc_coef) {
    if (order <= 0 || !raw_coef || !lpc_coef) {
        return;
    }
    if (order > 16) {
        order = 16;
    }

    int bits = (coef_res == 1 || coef_res == 4) ? 4 : 3;
    float iqfac = static_cast<float>(M_PI) / static_cast<float>(1 << bits);

    float gamma[16] = {0.0f};
    for (int k = 0; k < order; ++k) {
        int c = raw_coef[k];
        if (c >= (1 << (bits - 1))) {
            c -= (1 << bits);
        }
        gamma[k] = std::sin(static_cast<float>(c) * iqfac);
    }

    // Step-up recursion (Lattice to Direct-form LPC coefficients)
    float a[17] = {0.0f};
    float b[17] = {0.0f};
    a[0] = 1.0f;

    for (int m = 1; m <= order; ++m) {
        float gam = gamma[m - 1];
        b[0] = 1.0f;
        for (int j = 1; j < m; ++j) {
            b[j] = a[j] + gam * a[m - j];
        }
        b[m] = gam;
        for (int j = 1; j <= m; ++j) {
            a[j] = b[j];
        }
    }

    for (int k = 0; k < order; ++k) {
        lpc_coef[k] = a[k + 1];
    }
}

void apply_tns(float* spec,
               const TnsData& tns,
               const int* swb_offsets,
               size_t num_swb,
               WindowSequence seq) {
    if (!spec || !swb_offsets || num_swb == 0) {
        return;
    }

    size_t num_windows = (seq == WindowSequence::EightShort) ? AAC_NUM_SHORT_WINDOWS : 1;
    int window_len = swb_offsets[num_swb];

    for (size_t w = 0; w < num_windows; ++w) {
        float* win_spec = spec + w * window_len;
        uint8_t n_filt = tns.n_filt[w];
        if (n_filt > 4) {
            n_filt = 4;
        }

        for (uint8_t f = 0; f < n_filt; ++f) {
            const TnsFilter& filt = tns.filters[w][f];
            if (filt.order == 0 || filt.order > 16) {
                continue;
            }

            size_t start_band = filt.start_band;
            size_t stop_band = filt.stop_band;
            if (start_band >= num_swb) {
                continue;
            }
            if (stop_band > num_swb) {
                stop_band = num_swb;
            }
            if (start_band >= stop_band) {
                continue;
            }

            int start_line = swb_offsets[start_band];
            int stop_line = swb_offsets[stop_band];
            if (start_line >= stop_line) {
                continue;
            }

            float state[16] = {0.0f};

            if (filt.direction == 0) {
                // Upward filter (from low frequencies to high frequencies)
                for (int i = start_line; i < stop_line; ++i) {
                    float acc = win_spec[i];
                    for (int k = 0; k < filt.order; ++k) {
                        acc -= filt.coef[k] * state[k];
                    }
                    win_spec[i] = acc;
                    for (int k = filt.order - 1; k > 0; --k) {
                        state[k] = state[k - 1];
                    }
                    state[0] = acc;
                }
            } else {
                // Downward filter (from high frequencies down to low frequencies)
                for (int i = stop_line - 1; i >= start_line; --i) {
                    float acc = win_spec[i];
                    for (int k = 0; k < filt.order; ++k) {
                        acc -= filt.coef[k] * state[k];
                    }
                    win_spec[i] = acc;
                    for (int k = filt.order - 1; k > 0; --k) {
                        state[k] = state[k - 1];
                    }
                    state[0] = acc;
                }
            }
        }
    }
}

} // namespace audio_codecs::aac
