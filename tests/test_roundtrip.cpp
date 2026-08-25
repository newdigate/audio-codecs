// tests/test_roundtrip.cpp
#include "audio_codecs/mp3/mp3_encoder.h"
#include "audio_codecs/mp3/mp3_decoder.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::mp3;

    Mp3Encoder encoder;
    Mp3Decoder decoder;

    AudioConfig config{44100, 2, 192, false, 4};
    assert(encoder.init(config));
    assert(decoder.init(config));

    // Multi-frame continuous streaming test (5 consecutive frames)
    for (int frame = 0; frame < 5; ++frame) {
        float pcm_frame[2304] = {0.0f};
        for (int i = 0; i < 1152; ++i) {
            int t = frame * 1152 + i;
            float s = 0.5f * std::sin(constants::TWO_PI * 440.0f * t / 44100.0f);
            pcm_frame[i * 2]     = s;
            pcm_frame[i * 2 + 1] = s;
        }

        uint8_t frame_bytes[1024] = {0};
        int enc_bytes = encoder.encode_frame(pcm_frame, 2304, frame_bytes, sizeof(frame_bytes));
        assert(enc_bytes > 0);

        float dec_frame[2304] = {0.0f};
        int dec_count = decoder.decode_frame(frame_bytes, enc_bytes, dec_frame, 2304);
        assert(dec_count == 2304);

        // Verify decoded audio has non-zero signal
        if (frame >= 2) {
            float dec_energy = 0.0f;
            for (int i = 0; i < 2304; ++i) {
                dec_energy += dec_frame[i] * dec_frame[i];
            }
            assert(dec_energy > 0.0f);
        }
    }

    std::cout << "MP3 Multi-frame continuous stream encode/decode verified!\n";
    return 0;
}
