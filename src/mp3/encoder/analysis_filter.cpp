#include "src/mp3/encoder/analysis_filter.h"
#include "src/mp3/mp3_tables.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::mp3 {

AnalysisFilter::AnalysisFilter() {
    init_matrix();
    reset();
}

void AnalysisFilter::init_matrix() {
    if (initialized_) return;

    for (int k = 0; k < 32; ++k) {
        for (int i = 0; i < 64; ++i) {
            float angle = (constants::PI / 64.0f) * (2.0f * static_cast<float>(k) + 1.0f) * (static_cast<float>(i) - 16.0f);
            m_matrix_[k][i] = std::cos(angle);
        }
    }

    initialized_ = true;
}

void AnalysisFilter::reset() {
    std::memset(x_buffer_, 0, sizeof(x_buffer_));
}

void AnalysisFilter::filter_pcm(const float* in_pcm_32, float* out_subband_32) {
    if (!in_pcm_32 || !out_subband_32) return;
    if (!initialized_) init_matrix();

    // 1. Shift x_buffer_ right by 32
    std::memmove(x_buffer_ + 32, x_buffer_, (512 - 32) * sizeof(float));

    // 2. Insert new 32 PCM samples in reverse order into x_buffer_[0..31]
    for (int i = 0; i < 32; ++i) {
        x_buffer_[31 - i] = in_pcm_32[i];
    }

    // 3. Window x_buffer by C_ANALYSIS_WINDOW[512]
    float z[512];
    for (int i = 0; i < 512; ++i) {
        z[i] = x_buffer_[i] * C_ANALYSIS_WINDOW[i];
    }

    // 4. Calculate 64 values of Y
    float y[64];
    for (int i = 0; i < 64; ++i) {
        float sum = 0.0f;
        for (int j = 0; j < 8; ++j) {
            sum += z[i + 64 * j];
        }
        y[i] = sum;
    }

    // 5. Matrixing: calculate 32 subband samples
    for (int k = 0; k < 32; ++k) {
        float sum = 0.0f;
        for (int i = 0; i < 64; ++i) {
            sum += m_matrix_[k][i] * y[i];
        }
        out_subband_32[k] = sum;
    }
}

} // namespace audio_codecs::mp3
