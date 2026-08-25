#include "src/aac/encoder/quantizer.h"
#include "src/aac/encoder/huffman_encoder.h"
#include "src/aac/decoder/requantizer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::aac {

int AacQuantizer::quantize_single(float x, int sf) {
    if (x == 0.0f) return 0;

    float ax = std::fabs(x);
    float scale = std::pow(2.0f, -0.1875f * (static_cast<float>(sf) - 100.0f));
    float val = std::pow(ax, 0.75f) * scale + 0.4054f;
    int q = static_cast<int>(val);
    if (q > 8191) q = 8191;

    return (x < 0.0f) ? -q : q;
}

void AacQuantizer::quantize_spectrum_fast(const float* in_spectral, 
                                         const float* masking_thresholds, 
                                         const int* swb_offsets, 
                                         size_t num_swb, 
                                         int* out_quant, 
                                         int* out_scalefactors, 
                                         int& out_global_gain, 
                                         int target_bits) {
    if (!in_spectral || !swb_offsets || !out_quant || !out_scalefactors || num_swb == 0) {
        return;
    }

    int sf[AAC_MAX_SCALEFACTOR_BANDS] = {0};
    bool active[AAC_MAX_SCALEFACTOR_BANDS] = {false};

    // 1. Initial scalefactor estimation from masking thresholds
    for (size_t s = 0; s < num_swb && s < AAC_MAX_SCALEFACTOR_BANDS; ++s) {
        int start = swb_offsets[s];
        int end = swb_offsets[s + 1];
        float width = static_cast<float>(end - start);

        float max_val = 0.0f;
        for (int i = start; i < end; ++i) {
            float ax = std::fabs(in_spectral[i]);
            if (ax > max_val) max_val = ax;
        }

        if (max_val < 1e-6f) {
            sf[s] = 0;
            active[s] = false;
            for (int i = start; i < end; ++i) {
                out_quant[i] = 0;
            }
            continue;
        }

        active[s] = true;
        float thr = (masking_thresholds) ? std::max(masking_thresholds[s], 1e-9f) : 1.0f;

        // Noise energy ~ (width / 12) * 2^(0.5 * (sf - 100)) <= thr
        // sf = 100 + 2 * log2(12 * thr / width)
        float est_sf = 100.0f + 2.0f * (std::log((12.0f * thr) / width) / std::log(2.0f));

        // Ensure max value does not overflow 8191
        // sf >= 100 + 4 * log2(max_val / 8191^(4/3))
        // 8191^(4/3) ~ 166708
        float min_sf_for_max = 100.0f + 4.0f * (std::log(std::max(max_val / 160000.0f, 1e-12f)) / std::log(2.0f));
        est_sf = std::max(est_sf, min_sf_for_max);

        int isf = static_cast<int>(std::round(est_sf));
        sf[s] = std::max(0, std::min(255, isf));
    }

    // 2. Smooth scalefactors for DPCM constraints (deltas within [-60, +60])
    int first_active = -1;
    for (size_t s = 0; s < num_swb; ++s) {
        if (active[s]) {
            first_active = static_cast<int>(s);
            break;
        }
    }

    if (first_active == -1) {
        out_global_gain = 100;
        for (size_t s = 0; s < num_swb; ++s) {
            out_scalefactors[s] = 0;
        }
        return;
    }

    out_global_gain = sf[first_active];
    int last_sf = out_global_gain;

    for (size_t s = 0; s < num_swb; ++s) {
        if (active[s]) {
            int delta = sf[s] - last_sf;
            if (delta > 60) sf[s] = last_sf + 60;
            if (delta < -60) sf[s] = last_sf - 60;
            sf[s] = std::max(0, std::min(255, sf[s]));
            last_sf = sf[s];
        }
        out_scalefactors[s] = sf[s];
    }

    // 3. Forward quantize
    auto perform_quantization = [&]() {
        for (size_t s = 0; s < num_swb; ++s) {
            int start = swb_offsets[s];
            int end = swb_offsets[s + 1];
            if (!active[s]) {
                for (int i = start; i < end; ++i) {
                    out_quant[i] = 0;
                }
            } else {
                int s_val = out_scalefactors[s];
                for (int i = start; i < end; ++i) {
                    out_quant[i] = quantize_single(in_spectral[i], s_val);
                }
            }
        }
    };

    perform_quantization();

    // 4. Rate control adjustment if target_bits is specified
    if (target_bits > 0) {
        for (int iter = 0; iter < 4; ++iter) {
            size_t total_bits = 0;
            for (size_t s = 0; s < num_swb; ++s) {
                if (!active[s]) continue;
                int start = swb_offsets[s];
                int width = swb_offsets[s + 1] - start;
                int cb = AacHuffmanEncoder::find_best_codebook_for_band(&out_quant[start], width);
                total_bits += AacHuffmanEncoder::count_bits_spectral_band(&out_quant[start], width, cb);
            }

            if (total_bits > static_cast<size_t>(target_bits)) {
                // Too many bits -> increase scalefactor to make quantization coarser
                int step = (total_bits > static_cast<size_t>(target_bits * 1.5)) ? 2 : 1;
                out_global_gain = std::min(255, out_global_gain + step);
                for (size_t s = 0; s < num_swb; ++s) {
                    if (active[s]) {
                        out_scalefactors[s] = std::min(255, out_scalefactors[s] + step);
                    }
                }
                perform_quantization();
            } else {
                break;
            }
        }
    }
}

void AacQuantizer::quantize_spectrum_hq(const float* in_spectral, 
                                       const float* masking_thresholds, 
                                       const int* swb_offsets, 
                                       size_t num_swb, 
                                       int* out_quant, 
                                       int* out_scalefactors, 
                                       int& out_global_gain, 
                                       int target_bits) {
    // High-Quality mode falls back to fast mode with iterative rate tuning
    quantize_spectrum_fast(in_spectral, masking_thresholds, swb_offsets, num_swb,
                           out_quant, out_scalefactors, out_global_gain, target_bits);
}

} // namespace audio_codecs::aac
