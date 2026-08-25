#include "src/aac/decoder/requantizer.h"
#include <cmath>

namespace audio_codecs::aac {

namespace {

struct Pow2SfLut {
    float table[512]; // Scalefactor range [-128, 383], index = sf + 128

    Pow2SfLut() {
        for (int i = 0; i < 512; ++i) {
            int sf = i - 128;
            table[i] = std::pow(2.0f, 0.25f * (static_cast<float>(sf) - 100.0f));
        }
    }

    inline float get(int sf) const {
        int idx = sf + 128;
        if (idx >= 0 && idx < 512) {
            return table[idx];
        }
        return std::pow(2.0f, 0.25f * (static_cast<float>(sf) - 100.0f));
    }
};

static const Pow2SfLut g_pow2sf;

} // anonymous namespace

void requantize_spectrum(const int* quant_spectral, 
                         const int* scalefactors, 
                         const int* swb_offsets, 
                         size_t num_swb, 
                         float* out_float_spectral) {
    if (!quant_spectral || !scalefactors || !swb_offsets || !out_float_spectral || num_swb == 0) {
        return;
    }

    for (size_t b = 0; b < num_swb; ++b) {
        int start = swb_offsets[b];
        int end = swb_offsets[b + 1];
        float sf_scale = g_pow2sf.get(scalefactors[b]);

        for (int i = start; i < end; ++i) {
            int q = quant_spectral[i];
            if (q == 0) {
                out_float_spectral[i] = 0.0f;
            } else {
                float pow43 = dequant_pow43(q);
                out_float_spectral[i] = (q < 0) ? (-pow43 * sf_scale) : (pow43 * sf_scale);
            }
        }
    }
}

void requantize_short_spectrum(const int* quant_spectral, 
                               const int* scalefactors, 
                               const int* swb_offsets, 
                               size_t num_swb, 
                               size_t num_windows, 
                               float* out_float_spectral) {
    if (!quant_spectral || !scalefactors || !swb_offsets || !out_float_spectral || num_swb == 0 || num_windows == 0) {
        return;
    }

    size_t window_len = swb_offsets[num_swb];

    for (size_t w = 0; w < num_windows; ++w) {
        size_t win_offset = w * window_len;
        const int* win_sf = &scalefactors[w * num_swb];

        for (size_t b = 0; b < num_swb; ++b) {
            int start = swb_offsets[b];
            int end = swb_offsets[b + 1];
            float sf_scale = g_pow2sf.get(win_sf[b]);

            for (int i = start; i < end; ++i) {
                size_t global_idx = win_offset + i;
                int q = quant_spectral[global_idx];
                if (q == 0) {
                    out_float_spectral[global_idx] = 0.0f;
                } else {
                    float pow43 = dequant_pow43(q);
                    out_float_spectral[global_idx] = (q < 0) ? (-pow43 * sf_scale) : (pow43 * sf_scale);
                }
            }
        }
    }
}

} // namespace audio_codecs::aac
