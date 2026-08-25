#include "src/mp3/encoder/psychoacoustic.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::mp3 {

PsychoacousticModel::PsychoacousticModel() {
    init(44100);
}

void PsychoacousticModel::init(uint32_t sample_rate) {
    sample_rate_ = sample_rate;
    fft_.init();
    init_tables();
}

void PsychoacousticModel::reset() {
}

void PsychoacousticModel::init_tables() {
    // 1. Hanning window: 0.5 * (1 - cos(2*pi*n / 1024))
    for (int i = 0; i < 1024; ++i) {
        hanning_window_[i] = 0.5f * (1.0f - std::cos(constants::TWO_PI * static_cast<float>(i) / 1024.0f));
    }

    // 2. Precompute Absolute Threshold of Hearing (ATH) for each scalefactor band center freq
    const uint16_t* sfb_table = get_scalefac_band_table_long(sample_rate_);
    float bin_hz = static_cast<float>(sample_rate_) / 1024.0f;

    for (int s = 0; s < 22; ++s) {
        float center_bin = (sfb_table[s] + sfb_table[s + 1]) * 0.5f;
        float f_khz = (center_bin * bin_hz) / 1000.0f;
        f_khz = std::max(f_khz, 0.02f); // Avoid division by zero

        // Terhardt ATH approximation in dB:
        // ATH(f) = 3.64 * (f)^(-0.8) - 6.5 * exp(-0.6 * (f - 3.3)^2) + 1e-3 * f^4
        float f_pow = std::pow(f_khz, -0.8f);
        float exp_term = std::exp(-0.6f * (f_khz - 3.3f) * (f_khz - 3.3f));
        float f_4 = f_khz * f_khz * f_khz * f_khz;
        float ath_db = 3.64f * f_pow - 6.5f * exp_term + 0.001f * f_4;

        // Convert dB to linear power threshold
        ath_table_[s] = std::pow(10.0f, (ath_db - 90.0f) / 10.0f);
    }

    initialized_ = true;
}

void PsychoacousticModel::calculate_masking(const float* in_pcm_1024, float* mask_thresholds_22, float* smr_22) {
    if (!in_pcm_1024 || !mask_thresholds_22 || !smr_22) return;
    if (!initialized_) init_tables();

    // 1. Window input with Hanning window
    float windowed[1024];
    for (int i = 0; i < 1024; ++i) {
        windowed[i] = in_pcm_1024[i] * hanning_window_[i];
    }

    // 2. 1024-point FFT
    float real_out[513];
    float imag_out[513];
    fft_.transform_real(windowed, real_out, imag_out);

    // 3. Spectral Power: P[k] = Re^2 + Im^2
    float power[513];
    for (int k = 0; k < 513; ++k) {
        power[k] = real_out[k] * real_out[k] + imag_out[k] * imag_out[k];
    }

    // 4. Band energy calculation
    const uint16_t* sfb_table = get_scalefac_band_table_long(sample_rate_);
    float band_energy[22] = {0.0f};

    for (int s = 0; s < 22; ++s) {
        // Map 576 MDCT lines to 513 FFT bins
        int start_bin = std::min(static_cast<int>(sfb_table[s] * 512 / 576), 512);
        int end_bin   = std::min(static_cast<int>(sfb_table[s + 1] * 512 / 576), 512);
        if (end_bin <= start_bin) end_bin = start_bin + 1;

        float sum_e = 0.0f;
        for (int k = start_bin; k < end_bin && k < 513; ++k) {
            sum_e += power[k];
        }
        band_energy[s] = sum_e;
    }

    // 5. Spreading function & masking thresholds
    for (int s = 0; s < 22; ++s) {
        float spread_energy = 0.0f;
        for (int j = 0; j < 22; ++j) {
            int dz = std::abs(s - j);
            float weight = (dz == 0) ? 1.0f : (dz == 1 ? 0.3f : (dz == 2 ? 0.05f : 0.005f));
            spread_energy += band_energy[j] * weight;
        }

        // Masking offset: ~15 dB below spread energy
        float mask = spread_energy * 0.03162f; // 10^(-15/10)
        mask = std::max(mask, ath_table_[s]);

        mask_thresholds_22[s] = mask;

        // Signal-to-Mask Ratio (SMR) in dB
        float smr_val = 10.0f * std::log10((band_energy[s] + 1e-9f) / (mask + 1e-9f));
        smr_22[s] = smr_val;
    }
}

} // namespace audio_codecs::mp3
