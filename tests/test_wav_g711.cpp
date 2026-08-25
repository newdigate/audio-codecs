// tests/test_wav_g711.cpp
#include "src/wav/g711.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::wav;

    // Test A-law zero and boundaries
    uint8_t a_zero = linear16_to_alaw(0);
    int16_t a_zero_dec = alaw_to_linear16(a_zero);
    assert(std::abs(a_zero_dec) <= 8);

    uint8_t u_zero = linear16_to_mulaw(0);
    int16_t u_zero_dec = mulaw_to_linear16(u_zero);
    assert(u_zero_dec == 0);

    // Test full roundtrip across all 256 code points
    for (int code = 0; code < 256; ++code) {
        uint8_t a_in = static_cast<uint8_t>(code);
        int16_t pcm_a = alaw_to_linear16(a_in);
        uint8_t a_out = linear16_to_alaw(pcm_a);
        int16_t pcm_a_rt = alaw_to_linear16(a_out);
        assert(pcm_a == pcm_a_rt);
        assert(a_in == a_out);

        uint8_t u_in = static_cast<uint8_t>(code);
        int16_t pcm_u = mulaw_to_linear16(u_in);
        uint8_t u_out = linear16_to_mulaw(pcm_u);
        int16_t pcm_u_rt = mulaw_to_linear16(u_out);
        assert(pcm_u == pcm_u_rt);
        if (code != 0x7F) { // 0x7F is negative zero (-0), which re-encodes to positive zero (0xFF)
            assert(u_in == u_out);
        } else {
            assert(u_out == 0xFF);
        }
    }

    std::cout << "G.711 A-law and mu-law test passed!\n";
    return 0;
}
