#pragma once

#include <audio_codecs/slice/slice_types.h>
#include <cstdint>
#include <cstddef>

namespace audio_codecs::slice {

class SliceVoiceHelper {
public:
    static void compute_playback_params(const AslSlice& slice,
                                        uint32_t original_bpm_q16,
                                        uint32_t target_bpm_q16,
                                        uint32_t sample_rate,
                                        uint32_t* out_trigger_interval_samples,
                                        uint32_t* out_render_samples,
                                        bool* out_needs_tail_extension);

    static void apply_attack_ramp(int16_t* pcm, uint32_t count, uint32_t sample_rate);
    static void apply_choke_ramp(int16_t* pcm, uint32_t count, uint32_t sample_rate);
};

} // namespace audio_codecs::slice
