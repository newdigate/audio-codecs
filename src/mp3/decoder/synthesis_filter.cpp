#include "src/mp3/decoder/synthesis_filter.h"
#include "src/mp3/mp3_tables.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::mp3 {

SynthesisFilter::SynthesisFilter() {
    init_matrix();
    reset();
}

void SynthesisFilter::init_matrix() {
    if (initialized_) return;

    for (int i = 0; i < 64; ++i) {
        for (int k = 0; k < 32; ++k) {
            float angle = (constants::PI / 64.0f) * (16.0f + static_cast<float>(i)) * (2.0f * static_cast<float>(k) + 1.0f);
            n_matrix_[i][k] = std::cos(angle);
        }
    }

    initialized_ = true;
}

void SynthesisFilter::reset() {
    std::memset(v_buffer_, 0, sizeof(v_buffer_));
}

void SynthesisFilter::filter_subband(const float* subband_32, float* out_pcm_32) {
    if (!subband_32 || !out_pcm_32) return;
    if (!initialized_) init_matrix();

    // 1. Shift v_buffer_ right by 64
    std::memmove(v_buffer_ + 64, v_buffer_, (1024 - 64) * sizeof(float));

    // 2. Matrixing: calculate 64 values of V into v_buffer_[0..63]
    for (int i = 0; i < 64; ++i) {
        float sum = 0.0f;
        for (int k = 0; k < 32; ++k) {
            sum += subband_32[k] * n_matrix_[i][k];
        }
        v_buffer_[i] = sum;
    }

    // 3. Build 512-element vector U
    float u[512];
    for (int j = 0; j < 8; ++j) {
        for (int k = 0; k < 32; ++k) {
            u[j * 64 + k]      = v_buffer_[j * 128 + k];
            u[j * 64 + 32 + k] = v_buffer_[j * 128 + 96 + k];
        }
    }

    // 4. Window by D_SYNTHESIS_WINDOW[512] and calculate 32 output PCM samples
    for (int j = 0; j < 32; ++j) {
        float sum = 0.0f;
        for (int m = 0; m < 16; ++m) {
            int idx = j + 32 * m;
            sum += u[idx] * D_SYNTHESIS_WINDOW[idx];
        }
        out_pcm_32[j] = sum;
    }
}

} // namespace audio_codecs::mp3
