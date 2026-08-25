#pragma once
#include <cstdint>

namespace audio_codecs::wav {

extern const int16_t kALawToLinear16[256];
extern const int16_t kMuLawToLinear16[256];

inline int16_t alaw_to_linear16(uint8_t a_val) {
    return kALawToLinear16[a_val];
}

inline int16_t mulaw_to_linear16(uint8_t u_val) {
    return kMuLawToLinear16[u_val];
}

uint8_t linear16_to_alaw(int16_t pcm_val);
uint8_t linear16_to_mulaw(int16_t pcm_val);

} // namespace audio_codecs::wav
