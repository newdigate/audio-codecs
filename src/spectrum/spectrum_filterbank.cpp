#include "audio_codecs/spectrum/spectrum_filterbank.h"
#include <cmath>
#include <algorithm>

namespace audio_codecs::spectrum {

SpectrumFilterbank::SpectrumFilterbank() {
    init();
}

bool SpectrumFilterbank::init(uint32_t sample_rate, uint8_t num_bands, uint16_t min_freq, uint16_t max_freq) {
    initialized_ = false;

    if (sample_rate == 0 || min_freq == 0 || num_bands == 0) {
        return false;
    }

    // Clamp max_freq to Nyquist (sample_rate / 2) to prevent buffer over-read of in_magnitudes_512
    uint16_t nyquist = static_cast<uint16_t>(sample_rate / 2);
    if (max_freq > nyquist) {
        max_freq = nyquist;
    }

    if (max_freq <= min_freq) {
        return false;
    }

    if (num_bands > 64) num_bands = 64;
    num_bands_ = num_bands;

    constexpr float kPi = 3.14159265358979323846f;
    for (size_t i = 0; i < 1024; ++i) {
        float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) / 1023.0f));
        hann_window_[i] = static_cast<int16_t>(w * 32767.0f);
    }

    float bin_width = static_cast<float>(sample_rate) / 1024.0f;
    float f_min = static_cast<float>(min_freq);
    float f_max = static_cast<float>(max_freq);

    for (size_t b = 0; b < num_bands_; ++b) {
        float f_low = f_min * std::pow(f_max / f_min, static_cast<float>(b) / static_cast<float>(num_bands_));
        float f_high = f_min * std::pow(f_max / f_min, static_cast<float>(b + 1) / static_cast<float>(num_bands_));

        // Strictly clamp bin indices within [1, 511] to avoid out-of-bounds access
        uint16_t k_start = std::clamp(static_cast<uint16_t>(f_low / bin_width), uint16_t{1}, uint16_t{511});
        uint16_t k_end = std::clamp(static_cast<uint16_t>(std::ceil(f_high / bin_width)), uint16_t{1}, uint16_t{511});

        if (k_end < k_start) k_end = k_start;

        band_bin_start_[b] = k_start;
        band_bin_end_[b] = k_end;
    }

    initialized_ = true;
    return true;
}

void SpectrumFilterbank::apply_hann_window(const int16_t* in_pcm, int16_t* out_windowed_pcm) const {
    if (!in_pcm || !out_windowed_pcm) return;
    for (size_t i = 0; i < 1024; ++i) {
        int32_t val = (static_cast<int32_t>(in_pcm[i]) * static_cast<int32_t>(hann_window_[i])) >> 15;
        out_windowed_pcm[i] = static_cast<int16_t>(val);
    }
}

void SpectrumFilterbank::compute_bands(const float* in_magnitudes_512, uint8_t* out_bands) const {
    if (!in_magnitudes_512 || !out_bands) return;
    if (!initialized_) return;

    // Full-scale sine peak magnitude for 1024-point real FFT on 16-bit PCM is 32768.0f * 512.0f
    constexpr float kRefMag = 32768.0f * 512.0f;
    constexpr float kRefEnergy = kRefMag * kRefMag;

    for (size_t b = 0; b < num_bands_; ++b) {
        uint16_t start = band_bin_start_[b];
        uint16_t end = band_bin_end_[b];

        float sum_sq = 0.0f;
        for (uint16_t k = start; k <= end; ++k) {
            float mag = in_magnitudes_512[k];
            sum_sq += mag * mag;
        }

        // dB calculation with -96 dB floor normalized against full-scale FFT reference
        float norm_energy = (sum_sq / kRefEnergy) + 1e-10f;
        float db = 10.0f * std::log10(norm_energy);

        // Normalize dB from [-96, 0] to [0, 255]
        float scaled = (db + 96.0f) * (255.0f / 96.0f);
        out_bands[b] = static_cast<uint8_t>(std::clamp(scaled, 0.0f, 255.0f));
    }
}

} // namespace audio_codecs::spectrum
