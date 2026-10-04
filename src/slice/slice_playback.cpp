#include <audio_codecs/slice/slice_playback.h>
#include <algorithm>

namespace audio_codecs::slice {

void SliceVoiceHelper::compute_playback_params(const AslSlice& slice,
                                               uint32_t original_bpm_q16,
                                               uint32_t target_bpm_q16,
                                               uint32_t /*sample_rate*/,
                                               uint32_t* out_trigger_interval_samples,
                                               uint32_t* out_render_samples,
                                               bool* out_needs_tail_extension) {
    if (original_bpm_q16 == 0) original_bpm_q16 = (120 << 16);
    if (target_bpm_q16 == 0) target_bpm_q16 = original_bpm_q16;

    // trigger_interval = slice.length_samples * (original_bpm / target_bpm)
    uint64_t scaled = (static_cast<uint64_t>(slice.length_samples) * original_bpm_q16) / target_bpm_q16;
    uint32_t interval = static_cast<uint32_t>(scaled);

    if (out_trigger_interval_samples != nullptr) {
        *out_trigger_interval_samples = interval;
    }
    if (out_render_samples != nullptr) {
        *out_render_samples = (interval > slice.length_samples) ? interval : slice.length_samples;
    }
    if (out_needs_tail_extension != nullptr) {
        *out_needs_tail_extension = (interval > slice.length_samples);
    }
}

void SliceVoiceHelper::apply_attack_ramp(int16_t* pcm, uint32_t count, uint32_t sample_rate) {
    if (pcm == nullptr || count == 0 || sample_rate == 0) return;

    // 1.0 ms linear attack ramp
    uint32_t ramp_len = sample_rate / 1000;
    if (ramp_len == 0) return;
    if (ramp_len > count) ramp_len = count;

    for (uint32_t i = 0; i < ramp_len; ++i) {
        float gain = static_cast<float>(i) / static_cast<float>(ramp_len);
        pcm[i] = static_cast<int16_t>(pcm[i] * gain);
    }
}

void SliceVoiceHelper::apply_choke_ramp(int16_t* pcm, uint32_t count, uint32_t sample_rate) {
    if (pcm == nullptr || count == 0 || sample_rate == 0) return;

    // 2.5 ms linear choke release ramp
    uint32_t ramp_len = (sample_rate * 25) / 10000; // 2.5 ms
    if (ramp_len == 0) return;
    if (ramp_len > count) ramp_len = count;

    uint32_t start_idx = count - ramp_len;
    for (uint32_t i = 0; i < ramp_len; ++i) {
        float gain = 1.0f - (static_cast<float>(i + 1) / static_cast<float>(ramp_len));
        pcm[start_idx + i] = static_cast<int16_t>(pcm[start_idx + i] * gain);
    }
}

} // namespace audio_codecs::slice
