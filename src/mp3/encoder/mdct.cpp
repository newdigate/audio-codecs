#include "src/mp3/encoder/mdct.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::mp3 {

ForwardMdct::ForwardMdct() {
    init_tables();
    reset();
}

void ForwardMdct::init_tables() {
    if (initialized_) return;

    // Precompute 36-point forward MDCT cosine table: cos((pi/72) * (2i + 1 + 18) * (2k + 1))
    for (int k = 0; k < 18; ++k) {
        for (int i = 0; i < 36; ++i) {
            float angle = (constants::PI / 72.0f) * (2.0f * i + 1.0f + 18.0f) * (2.0f * k + 1.0f);
            cos_36_[k][i] = std::cos(angle);
        }
    }

    // Precompute 12-point forward MDCT cosine table
    for (int k = 0; k < 6; ++k) {
        for (int i = 0; i < 12; ++i) {
            float angle = (constants::PI / 24.0f) * (2.0f * i + 1.0f + 6.0f) * (2.0f * k + 1.0f);
            cos_12_[k][i] = std::cos(angle);
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

void ForwardMdct::reset() {
}

void ForwardMdct::transform_subband(const float* time_36, float* mdct_18, int block_type) {
    if (!time_36 || !mdct_18) return;
    if (!initialized_) init_tables();

    if (block_type == 2) {
        // Short blocks: 3x 12-point forward MDCTs
        for (int w = 0; w < 3; ++w) {
            const float* in_12 = &time_36[6 + w * 6];
            float* out_6 = &mdct_18[w * 6];

            for (int k = 0; k < 6; ++k) {
                float sum = 0.0f;
                for (int i = 0; i < 12; ++i) {
                    sum += in_12[i] * win_short_[i] * cos_12_[k][i];
                }
                out_6[k] = sum;
            }
        }
    } else {
        // Long blocks: 36-point forward MDCT with window
        const float* win = (block_type == 1) ? win_start_ :
                           (block_type == 3) ? win_stop_ : win_normal_;

        for (int k = 0; k < 18; ++k) {
            float sum = 0.0f;
            for (int i = 0; i < 36; ++i) {
                sum += time_36[i] * win[i] * cos_36_[k][i];
            }
            mdct_18[k] = sum;
        }
    }
}

} // namespace audio_codecs::mp3
