#include "src/mp3/decoder/imdct.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::mp3 {

ImdctEngine::ImdctEngine() {
    init_tables();
    reset();
}

void ImdctEngine::init_tables() {
    if (initialized_) return;

    // Precompute 36-point IMDCT cosine table
    for (int i = 0; i < 36; ++i) {
        for (int k = 0; k < 18; ++k) {
            float angle = (constants::PI / 72.0f) * (2.0f * i + 1.0f + 18.0f) * (2.0f * k + 1.0f);
            cos_36_[i][k] = std::cos(angle);
        }
    }

    // Precompute 12-point IMDCT cosine table
    for (int i = 0; i < 12; ++i) {
        for (int k = 0; k < 6; ++k) {
            float angle = (constants::PI / 24.0f) * (2.0f * i + 1.0f + 6.0f) * (2.0f * k + 1.0f);
            cos_12_[i][k] = std::cos(angle);
        }
    }

    // Precompute windows
    for (int i = 0; i < 36; ++i) {
        win_normal_[i] = std::sin((constants::PI / 36.0f) * (i + 0.5f));
    }

    for (int i = 0; i < 12; ++i) {
        win_short_[i] = std::sin((constants::PI / 12.0f) * (i + 0.5f));
    }

    for (int i = 0; i < 36; ++i) {
        if (i < 18) {
            win_start_[i] = std::sin((constants::PI / 36.0f) * (i + 0.5f));
        } else if (i < 24) {
            win_start_[i] = 1.0f;
        } else if (i < 30) {
            win_start_[i] = std::sin((constants::PI / 12.0f) * (i - 18 + 0.5f));
        } else {
            win_start_[i] = 0.0f;
        }
    }

    for (int i = 0; i < 36; ++i) {
        if (i < 6) {
            win_stop_[i] = 0.0f;
        } else if (i < 12) {
            win_stop_[i] = std::sin((constants::PI / 12.0f) * (i - 6 + 0.5f));
        } else if (i < 18) {
            win_stop_[i] = 1.0f;
        } else {
            win_stop_[i] = std::sin((constants::PI / 36.0f) * (i + 0.5f));
        }
    }

    initialized_ = true;
}

void ImdctEngine::reset() {
    std::memset(overlap_, 0, sizeof(overlap_));
}

void ImdctEngine::transform_subband(const float* xr_18, int ch, int sb, float* out_18, int block_type) {
    if (!xr_18 || !out_18 || ch < 0 || ch >= 2 || sb < 0 || sb >= 32) return;
    if (!initialized_) init_tables();

    float raw_36[36];

    if (block_type == 2) {
        // Short blocks: 3 separate 12-point IMDCTs
        std::memset(raw_36, 0, sizeof(raw_36));

        for (int w = 0; w < 3; ++w) {
            const float* in_6 = &xr_18[w * 6];
            float s_12[12];

            for (int i = 0; i < 12; ++i) {
                float sum = 0.0f;
                for (int k = 0; k < 6; ++k) {
                    sum += in_6[k] * cos_12_[i][k];
                }
                s_12[i] = sum * win_short_[i];
            }

            // Overlap and add the three short blocks with 6-sample offsets
            int offset = 6 + w * 6;
            for (int i = 0; i < 12; ++i) {
                raw_36[offset + i] += s_12[i];
            }
        }
    } else {
        // Long blocks: 36-point IMDCT with selected window
        const float* win = (block_type == 1) ? win_start_ :
                           (block_type == 3) ? win_stop_ : win_normal_;

        for (int i = 0; i < 36; ++i) {
            float sum = 0.0f;
            for (int k = 0; k < 18; ++k) {
                sum += xr_18[k] * cos_36_[i][k];
            }
            raw_36[i] = sum * win[i];
        }
    }

    // Overlap-add with previous second half
    for (int i = 0; i < 18; ++i) {
        out_18[i] = raw_36[i] + overlap_[ch][sb][i];
        overlap_[ch][sb][i] = raw_36[i + 18];
    }
}

} // namespace audio_codecs::mp3
