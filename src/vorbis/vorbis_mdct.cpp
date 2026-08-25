#include "src/vorbis/vorbis_mdct.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::vorbis {

namespace {

constexpr double kPi = 3.14159265358979323846;

} // namespace

VorbisMdct::VorbisMdct(size_t n) {
    init(n);
}

bool VorbisMdct::init(size_t n) {
    if (n < 4 || (n & (n - 1)) != 0 || n > kMaxN) {
        return false;
    }
    n_ = n;
    log2n_ = 0;
    size_t temp = n;
    while (temp > 1) {
        log2n_++;
        temp >>= 1;
    }

    init_tables();
    return true;
}

void VorbisMdct::init_tables() {
    size_t n = n_;
    for (size_t i = 0; i < n; ++i) {
        double angle = (2.0 * kPi / static_cast<double>(n)) * (static_cast<double>(i) + 0.5);
        trig_[i] = static_cast<float>(std::cos(angle));
    }
}

void VorbisMdct::forward_mdct(const float* in_time, float* out_freq) const {
    if (!in_time || !out_freq || n_ == 0) return;

    size_t n = n_;
    size_t n2 = n / 2;
    double scale = 2.0 * kPi / static_cast<double>(n);

    for (size_t k = 0; k < n2; ++k) {
        double sum = 0.0;
        double k_term = (static_cast<double>(k) + 0.5) * scale;
        for (size_t i = 0; i < n; ++i) {
            double angle = (static_cast<double>(i) + 0.5 + static_cast<double>(n) * 0.25) * k_term;
            sum += static_cast<double>(in_time[i]) * std::cos(angle);
        }
        out_freq[k] = static_cast<float>(sum);
    }
}

void VorbisMdct::inverse_imdct(const float* in_freq, float* out_time) const {
    if (!in_freq || !out_time || n_ == 0) return;

    size_t n = n_;
    size_t n2 = n / 2;
    double norm = 4.0 / static_cast<double>(n);
    double scale = 2.0 * kPi / static_cast<double>(n);

    for (size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        double i_term = (static_cast<double>(i) + 0.5 + static_cast<double>(n) * 0.25) * scale;
        for (size_t k = 0; k < n2; ++k) {
            double angle = (static_cast<double>(k) + 0.5) * i_term;
            sum += static_cast<double>(in_freq[k]) * std::cos(angle);
        }
        out_time[i] = static_cast<float>(sum * norm);
    }
}

} // namespace audio_codecs::vorbis
