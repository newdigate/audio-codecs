#include "src/aac/encoder/psychoacoustic.h"
#include "src/aac/aac_tables.h"
#include "src/core/math_constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::aac {

PsychoacousticModel::PsychoacousticModel() {
    init(44100);
}

void PsychoacousticModel::init(uint32_t sample_rate) {
    sample_rate_ = sample_rate;
    fft_long_.init();
    fft_short_.init();
    init_tables();
}

void PsychoacousticModel::reset() {
}

void PsychoacousticModel::init_tables() {
    // 1. Hanning window for long window (2048)
    for (size_t i = 0; i < AAC_WINDOW_LEN_LONG; ++i) {
        hanning_long_[i] = 0.5f * (1.0f - std::cos(constants::TWO_PI * static_cast<float>(i) / static_cast<float>(AAC_WINDOW_LEN_LONG)));
    }

    // 2. Hanning window for short window (256)
    for (size_t i = 0; i < AAC_WINDOW_LEN_SHORT; ++i) {
        hanning_short_[i] = 0.5f * (1.0f - std::cos(constants::TWO_PI * static_cast<float>(i) / static_cast<float>(AAC_WINDOW_LEN_SHORT)));
    }

    // 3. Precompute ATH for long SWBs
    size_t num_swb_long = 0;
    const int* swb_long = get_swb_offset_long(sample_rate_, num_swb_long);
    if (swb_long && num_swb_long > 0) {
        float bin_hz = static_cast<float>(sample_rate_) / static_cast<float>(AAC_WINDOW_LEN_LONG);
        for (size_t s = 0; s < num_swb_long && s < AAC_MAX_SCALEFACTOR_BANDS; ++s) {
            float center_bin = 0.5f * static_cast<float>(swb_long[s] + swb_long[s + 1]);
            float f_khz = std::max((center_bin * bin_hz) / 1000.0f, 0.02f);

            float f_pow = std::pow(f_khz, -0.8f);
            float exp_term = std::exp(-0.6f * (f_khz - 3.3f) * (f_khz - 3.3f));
            float f_4 = f_khz * f_khz * f_khz * f_khz;
            float ath_db = 3.64f * f_pow - 6.5f * exp_term + 0.001f * f_4;

            float width = static_cast<float>(swb_long[s + 1] - swb_long[s]);
            ath_table_long_[s] = std::pow(10.0f, (ath_db - 90.0f) / 10.0f) * width;
        }
    }

    // 4. Precompute ATH for short SWBs
    size_t num_swb_short = 0;
    const int* swb_short = get_swb_offset_short(sample_rate_, num_swb_short);
    if (swb_short && num_swb_short > 0) {
        float bin_hz = static_cast<float>(sample_rate_) / static_cast<float>(AAC_WINDOW_LEN_SHORT);
        for (size_t s = 0; s < num_swb_short && s < AAC_MAX_SCALEFACTOR_BANDS; ++s) {
            float center_bin = 0.5f * static_cast<float>(swb_short[s] + swb_short[s + 1]);
            float f_khz = std::max((center_bin * bin_hz) / 1000.0f, 0.02f);

            float f_pow = std::pow(f_khz, -0.8f);
            float exp_term = std::exp(-0.6f * (f_khz - 3.3f) * (f_khz - 3.3f));
            float f_4 = f_khz * f_khz * f_khz * f_khz;
            float ath_db = 3.64f * f_pow - 6.5f * exp_term + 0.001f * f_4;

            float width = static_cast<float>(swb_short[s + 1] - swb_short[s]);
            ath_table_short_[s] = std::pow(10.0f, (ath_db - 90.0f) / 10.0f) * width;
        }
    }

    initialized_ = true;
}

void PsychoacousticModel::analyze_long(const float* pcm_2048, 
                                      uint32_t sample_rate, 
                                      float* out_thresholds, 
                                      float* out_energy, 
                                      float& out_pe) {
    if (!pcm_2048 || !out_thresholds || !out_energy) return;
    if (!initialized_ || sample_rate_ != sample_rate) {
        init(sample_rate);
    }

    size_t num_swb = 0;
    const int* swb = get_swb_offset_long(sample_rate_, num_swb);
    if (!swb || num_swb == 0) return;

    // 1. Window PCM
    float windowed[AAC_WINDOW_LEN_LONG];
    for (size_t i = 0; i < AAC_WINDOW_LEN_LONG; ++i) {
        windowed[i] = pcm_2048[i] * hanning_long_[i];
    }

    // 2. 2048-point FFT
    float real_out[core::Fft2048::NUM_BINS];
    float imag_out[core::Fft2048::NUM_BINS];
    fft_long_.transform_real(windowed, real_out, imag_out);

    // 3. Power spectrum
    float power[core::Fft2048::NUM_BINS];
    for (size_t k = 0; k < core::Fft2048::NUM_BINS; ++k) {
        power[k] = real_out[k] * real_out[k] + imag_out[k] * imag_out[k];
    }

    // 4. Band energy and tonality
    float band_energy[AAC_MAX_SCALEFACTOR_BANDS] = {0.0f};
    float ratio_power[AAC_MAX_SCALEFACTOR_BANDS] = {0.0f};

    for (size_t s = 0; s < num_swb; ++s) {
        int start = swb[s];
        int end = swb[s + 1];
        float width = static_cast<float>(end - start);

        float sum_e = 0.0f;
        float log_sum = 0.0f;
        for (int k = start; k < end && k < static_cast<int>(core::Fft2048::NUM_BINS); ++k) {
            float p = power[k];
            sum_e += p;
            log_sum += std::log(p + 1e-20f);
        }

        band_energy[s] = sum_e;
        out_energy[s] = sum_e;

        float geom_mean = std::exp(log_sum / width);
        float arith_mean = (sum_e + 1e-20f) / width;
        float sfm = geom_mean / arith_mean;
        float sfm_db = 10.0f * std::log10(std::max(sfm, 1e-6f));

        // Tonality index alpha in [0, 1]
        float alpha = std::min(1.0f, std::max(0.0f, sfm_db / -60.0f));
        float offset_db = alpha * 29.0f + (1.0f - alpha) * 6.0f;
        ratio_power[s] = std::pow(10.0f, -offset_db / 10.0f);
    }

    // 5. Spreading function and masking thresholds
    float pe_acc = 0.0f;
    for (size_t s = 0; s < num_swb; ++s) {
        float spread_energy = 0.0f;
        for (size_t j = 0; j < num_swb; ++j) {
            int dz = static_cast<int>(s) - static_cast<int>(j);
            float weight = 1.0f;
            if (dz > 0) {
                weight = std::pow(0.3f, static_cast<float>(dz));
            } else if (dz < 0) {
                weight = std::pow(0.25f, static_cast<float>(-dz));
            }
            spread_energy += band_energy[j] * weight;
        }

        float mask = spread_energy * ratio_power[s];
        mask = std::max(mask, ath_table_long_[s]);
        out_thresholds[s] = mask;

        // Perceptual entropy: PE = width * log2(max(1, E / thr))
        float width = static_cast<float>(swb[s + 1] - swb[s]);
        float snr_ratio = (band_energy[s] + 1e-9f) / (mask + 1e-9f);
        if (snr_ratio > 1.0f) {
            pe_acc += width * (std::log(snr_ratio) / std::log(2.0f));
        }
    }

    out_pe = pe_acc;
}

void PsychoacousticModel::analyze_short(const float* pcm_256, 
                                       uint32_t sample_rate, 
                                       float* out_thresholds, 
                                       float* out_energy, 
                                       float& out_pe) {
    if (!pcm_256 || !out_thresholds || !out_energy) return;
    if (!initialized_ || sample_rate_ != sample_rate) {
        init(sample_rate);
    }

    size_t num_swb = 0;
    const int* swb = get_swb_offset_short(sample_rate_, num_swb);
    if (!swb || num_swb == 0) return;

    // 1. Window PCM
    float windowed[AAC_WINDOW_LEN_SHORT];
    for (size_t i = 0; i < AAC_WINDOW_LEN_SHORT; ++i) {
        windowed[i] = pcm_256[i] * hanning_short_[i];
    }

    // 2. 256-point FFT
    float real_out[core::Fft256::NUM_BINS];
    float imag_out[core::Fft256::NUM_BINS];
    fft_short_.transform_real(windowed, real_out, imag_out);

    // 3. Power spectrum
    float power[core::Fft256::NUM_BINS];
    for (size_t k = 0; k < core::Fft256::NUM_BINS; ++k) {
        power[k] = real_out[k] * real_out[k] + imag_out[k] * imag_out[k];
    }

    // 4. Band energy
    float band_energy[AAC_MAX_SCALEFACTOR_BANDS] = {0.0f};
    for (size_t s = 0; s < num_swb; ++s) {
        int start = swb[s];
        int end = swb[s + 1];

        float sum_e = 0.0f;
        for (int k = start; k < end && k < static_cast<int>(core::Fft256::NUM_BINS); ++k) {
            sum_e += power[k];
        }

        band_energy[s] = sum_e;
        out_energy[s] = sum_e;
    }

    // 5. Spreading function & thresholds
    float pe_acc = 0.0f;
    for (size_t s = 0; s < num_swb; ++s) {
        float spread_energy = 0.0f;
        for (size_t j = 0; j < num_swb; ++j) {
            int dz = static_cast<int>(s) - static_cast<int>(j);
            float weight = (dz == 0) ? 1.0f : (dz > 0 ? std::pow(0.3f, static_cast<float>(dz)) : std::pow(0.25f, static_cast<float>(-dz)));
            spread_energy += band_energy[j] * weight;
        }

        float mask = spread_energy * 0.03162f; // ~15 dB below spread energy
        mask = std::max(mask, ath_table_short_[s]);
        out_thresholds[s] = mask;

        float width = static_cast<float>(swb[s + 1] - swb[s]);
        float snr_ratio = (band_energy[s] + 1e-9f) / (mask + 1e-9f);
        if (snr_ratio > 1.0f) {
            pe_acc += width * (std::log(snr_ratio) / std::log(2.0f));
        }
    }

    out_pe = pe_acc;
}

} // namespace audio_codecs::aac
