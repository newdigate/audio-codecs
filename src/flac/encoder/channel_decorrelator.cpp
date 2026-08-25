#include "src/flac/encoder/channel_decorrelator.h"
#include <cmath>
#include <cstdlib>

namespace audio_codecs::flac {

void ChannelDecorrelatorEncoder::apply_decorrelation(const int32_t* left, 
                                                    const int32_t* right, 
                                                    size_t count, 
                                                    int32_t* out_ch0, 
                                                    int32_t* out_ch1, 
                                                    FlacChannelAssignment mode) {
    if (!left || !right || !out_ch0 || !out_ch1 || count == 0) return;

    switch (mode) {
        case FlacChannelAssignment::Independent:
            for (size_t i = 0; i < count; ++i) {
                out_ch0[i] = left[i];
                out_ch1[i] = right[i];
            }
            break;

        case FlacChannelAssignment::LeftSide:
            for (size_t i = 0; i < count; ++i) {
                out_ch0[i] = left[i];
                out_ch1[i] = left[i] - right[i];
            }
            break;

        case FlacChannelAssignment::RightSide:
            for (size_t i = 0; i < count; ++i) {
                out_ch0[i] = left[i] - right[i];
                out_ch1[i] = right[i];
            }
            break;

        case FlacChannelAssignment::MidSide:
            for (size_t i = 0; i < count; ++i) {
                out_ch0[i] = (left[i] + right[i]) >> 1;
                out_ch1[i] = left[i] - right[i];
            }
            break;
    }
}

FlacChannelAssignment ChannelDecorrelatorEncoder::select_optimal_mode(const int32_t* left, 
                                                                     const int32_t* right, 
                                                                     size_t count) {
    if (!left || !right || count == 0) return FlacChannelAssignment::Independent;

    uint64_t energy_indep = 0;
    uint64_t energy_left_side = 0;
    uint64_t energy_right_side = 0;
    uint64_t energy_mid_side = 0;

    for (size_t i = 0; i < count; ++i) {
        int64_t l = left[i];
        int64_t r = right[i];
        int64_t side = l - r;
        int64_t mid = (l + r) >> 1;

        energy_indep += std::abs(l) + std::abs(r);
        energy_left_side += std::abs(l) + std::abs(side);
        energy_right_side += std::abs(side) + std::abs(r);
        energy_mid_side += std::abs(mid) + std::abs(side);
    }

    uint64_t min_energy = energy_indep;
    FlacChannelAssignment best = FlacChannelAssignment::Independent;

    if (energy_left_side < min_energy) {
        min_energy = energy_left_side;
        best = FlacChannelAssignment::LeftSide;
    }
    if (energy_right_side < min_energy) {
        min_energy = energy_right_side;
        best = FlacChannelAssignment::RightSide;
    }
    if (energy_mid_side < min_energy) {
        min_energy = energy_mid_side;
        best = FlacChannelAssignment::MidSide;
    }

    return best;
}

} // namespace audio_codecs::flac
