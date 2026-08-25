#include "src/aac/decoder/stereo_processor.h"
#include <cmath>

namespace audio_codecs::aac {

void apply_ms_stereo(float* left_spec,
                     float* right_spec,
                     const uint8_t* ms_used,
                     const int* swb_offsets,
                     size_t num_swb) {
    if (!left_spec || !right_spec || !ms_used || !swb_offsets) {
        return;
    }
    for (size_t s = 0; s < num_swb; ++s) {
        if (ms_used[s] != 0) {
            int start = swb_offsets[s];
            int end = swb_offsets[s + 1];
            for (int i = start; i < end; ++i) {
                float m = left_spec[i];
                float s_val = right_spec[i];
                left_spec[i] = m + s_val;
                right_spec[i] = m - s_val;
            }
        }
    }
}

void apply_ms_stereo_short(float* left_spec,
                           float* right_spec,
                           const uint8_t* ms_used,
                           const int* swb_offsets,
                           size_t num_swb,
                           size_t num_windows) {
    if (!left_spec || !right_spec || !ms_used || !swb_offsets) {
        return;
    }
    int window_len = swb_offsets[num_swb];
    for (size_t w = 0; w < num_windows; ++w) {
        int win_off = static_cast<int>(w) * window_len;
        for (size_t s = 0; s < num_swb; ++s) {
            if (ms_used[w * num_swb + s] != 0) {
                int start = swb_offsets[s];
                int end = swb_offsets[s + 1];
                for (int i = start; i < end; ++i) {
                    float m = left_spec[win_off + i];
                    float s_val = right_spec[win_off + i];
                    left_spec[win_off + i] = m + s_val;
                    right_spec[win_off + i] = m - s_val;
                }
            }
        }
    }
}

void apply_intensity_stereo(const float* left_spec,
                            float* right_spec,
                            const int* is_pos,
                            const uint8_t* is_type,
                            const int* swb_offsets,
                            size_t num_swb) {
    if (!left_spec || !right_spec || !is_pos || !is_type || !swb_offsets) {
        return;
    }
    for (size_t s = 0; s < num_swb; ++s) {
        if (is_type[s] != 0) {
            float sign = (is_type[s] == 2 || is_type[s] == 14) ? -1.0f : 1.0f;
            float scale = sign * std::pow(0.5f, 0.25f * static_cast<float>(is_pos[s]));
            int start = swb_offsets[s];
            int end = swb_offsets[s + 1];
            for (int i = start; i < end; ++i) {
                right_spec[i] = left_spec[i] * scale;
            }
        }
    }
}

void apply_intensity_stereo_short(const float* left_spec,
                                  float* right_spec,
                                  const int* is_pos,
                                  const uint8_t* is_type,
                                  const int* swb_offsets,
                                  size_t num_swb,
                                  size_t num_windows) {
    if (!left_spec || !right_spec || !is_pos || !is_type || !swb_offsets) {
        return;
    }
    int window_len = swb_offsets[num_swb];
    for (size_t w = 0; w < num_windows; ++w) {
        int win_off = static_cast<int>(w) * window_len;
        for (size_t s = 0; s < num_swb; ++s) {
            size_t idx = w * num_swb + s;
            if (is_type[idx] != 0) {
                float sign = (is_type[idx] == 2 || is_type[idx] == 14) ? -1.0f : 1.0f;
                float scale = sign * std::pow(0.5f, 0.25f * static_cast<float>(is_pos[idx]));
                int start = swb_offsets[s];
                int end = swb_offsets[s + 1];
                for (int i = start; i < end; ++i) {
                    right_spec[win_off + i] = left_spec[win_off + i] * scale;
                }
            }
        }
    }
}

void apply_pns(float* spec,
               const uint8_t* pns_active,
               const int* pns_energy,
               const int* swb_offsets,
               size_t num_swb,
               uint32_t& rng_state) {
    if (!spec || !pns_active || !pns_energy || !swb_offsets) {
        return;
    }
    for (size_t s = 0; s < num_swb; ++s) {
        if (pns_active[s] != 0) {
            int start = swb_offsets[s];
            int end = swb_offsets[s + 1];
            int n = end - start;
            if (n <= 0) continue;

            float energy_sum = 0.0f;
            for (int i = start; i < end; ++i) {
                rng_state = rng_state * 1664525u + 1013904223u;
                float val = static_cast<float>(static_cast<int32_t>(rng_state));
                spec[i] = val;
                energy_sum += val * val;
            }
            float target_scale = std::pow(2.0f, 0.25f * static_cast<float>(pns_energy[s] - 100));
            float norm = (energy_sum > 1e-12f) ? (target_scale / std::sqrt(energy_sum)) : 0.0f;
            for (int i = start; i < end; ++i) {
                spec[i] *= norm;
            }
        }
    }
}

void apply_pns_short(float* spec,
                     const uint8_t* pns_active,
                     const int* pns_energy,
                     const int* swb_offsets,
                     size_t num_swb,
                     size_t num_windows,
                     uint32_t& rng_state) {
    if (!spec || !pns_active || !pns_energy || !swb_offsets) {
        return;
    }
    int window_len = swb_offsets[num_swb];
    for (size_t w = 0; w < num_windows; ++w) {
        int win_off = static_cast<int>(w) * window_len;
        for (size_t s = 0; s < num_swb; ++s) {
            size_t idx = w * num_swb + s;
            if (pns_active[idx] != 0) {
                int start = swb_offsets[s];
                int end = swb_offsets[s + 1];
                int n = end - start;
                if (n <= 0) continue;

                float energy_sum = 0.0f;
                for (int i = start; i < end; ++i) {
                    rng_state = rng_state * 1664525u + 1013904223u;
                    float val = static_cast<float>(static_cast<int32_t>(rng_state));
                    spec[win_off + i] = val;
                    energy_sum += val * val;
                }
                float target_scale = std::pow(2.0f, 0.25f * static_cast<float>(pns_energy[idx] - 100));
                float norm = (energy_sum > 1e-12f) ? (target_scale / std::sqrt(energy_sum)) : 0.0f;
                for (int i = start; i < end; ++i) {
                    spec[win_off + i] *= norm;
                }
            }
        }
    }
}

} // namespace audio_codecs::aac
