// tests/test_flac_encoder.cpp
#include "audio_codecs/flac/flac_encoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    FlacEncoder encoder;
    FlacEncoderConfig cfg;
    cfg.core_config = {44100, 2, 0, false, 2};
    cfg.compression_level = 5;
    cfg.block_size = 1152;
    cfg.bit_depth = 16;
    assert(encoder.init_flac(cfg));

    int16_t pcm_in[1152 * 2] = {0};
    for (int i = 0; i < 1152; ++i) {
        pcm_in[i * 2]     = static_cast<int16_t>(i * 10);
        pcm_in[i * 2 + 1] = static_cast<int16_t>(-i * 10);
    }

    uint8_t out[4096] = {0};
    int bytes = encoder.encode_frame_i16(pcm_in, 1152 * 2, out, sizeof(out));
    assert(bytes > 0);
    assert(out[0] == 0xFF && (out[1] & 0xFC) == 0xF8); // Sync code 0b11111111111110

    std::cout << "FLAC Encoder facade test passed! (Encoded " << bytes << " bytes)\n";
    return 0;
}
