// tests/test_mp3_encoder.cpp
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
    AudioConfig config{44100, 2, 128, false, 4};
    assert(encoder.init(config));

    // 1152 samples * 2 channels = 2304 interleaved floats
    float pcm_in[2304] = {0.0f};
    for (int i = 0; i < 1152; ++i) {
        float sample = 0.5f * std::sin(constants::TWO_PI * 440.0f * i / 44100.0f);
        pcm_in[i * 2]     = sample;
        pcm_in[i * 2 + 1] = sample;
    }

    uint8_t mp3_out[1024] = {0};
    int bytes_encoded = encoder.encode_frame(pcm_in, 2304, mp3_out, sizeof(mp3_out));

    std::cout << "bytes_encoded: " << bytes_encoded << ", expected: " << 417 << "\n";
    assert(bytes_encoded == 417); // 128 kbps at 44.1 kHz = 417 bytes/frame
    assert(mp3_out[0] == 0xFF);
    assert((mp3_out[1] & 0xE0) == 0xE0); // Syncword 11 bits

    std::cout << "MP3 encoder test passed!\n";
    return 0;
}
