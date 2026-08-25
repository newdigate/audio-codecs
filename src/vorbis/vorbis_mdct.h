#pragma once
#include "audio_codecs/vorbis/vorbis_types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_codecs::vorbis {

class VorbisMdct {
public:
    VorbisMdct() = default;
    explicit VorbisMdct(size_t n);

    bool init(size_t n);
    size_t size() const { return n_; }

    // Forward MDCT: N time samples -> N/2 frequency bins
    void forward_mdct(const float* in_time, float* out_freq) const;

    // Inverse IMDCT: N/2 frequency bins -> N time samples
    void inverse_imdct(const float* in_freq, float* out_time) const;

private:
    size_t n_{0};
    size_t log2n_{0};

    // Pre-allocated static twiddle lookup tables for up to VORBIS_MAX_BLOCK_SIZE
    static constexpr size_t kMaxN = VORBIS_MAX_BLOCK_SIZE;
    float trig_[kMaxN]{0};
    uint32_t bitrev_[kMaxN / 4]{0};

    void init_tables();
};

} // namespace audio_codecs::vorbis
