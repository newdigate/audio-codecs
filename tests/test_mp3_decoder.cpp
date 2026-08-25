// tests/test_mp3_decoder.cpp
#include "audio_codecs/mp3/mp3_decoder.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::mp3;
    using namespace audio_codecs::core;

    Mp3Decoder decoder;
    AudioConfig config{44100, 2, 128, false, 4};
    assert(decoder.init(config));

    // Construct a minimal valid MPEG-1 Layer 3 silent frame:
    // Header: 0xFFFB9064 (44.1kHz, 128kbps, Joint Stereo MS, no CRC, no padding -> 417 bytes)
    // Side info: 32 bytes (main_data_begin = 0, all big_values = 0, part2_3_length = 0)
    // Main data: 417 - 4 - 32 = 381 bytes of zeros
    uint8_t frame_buf[417] = {0};
    frame_buf[0] = 0xFF;
    frame_buf[1] = 0xFB;
    frame_buf[2] = 0x90;
    frame_buf[3] = 0x64;

    float out_pcm[2304] = {0.0f};
    int samples_decoded = decoder.decode_frame(frame_buf, sizeof(frame_buf), out_pcm, 2304);

    assert(samples_decoded == 2304); // 1152 samples * 2 channels
    // Verify silent frame produces zeros
    for (int i = 0; i < 2304; ++i) {
        assert(std::fabs(out_pcm[i]) < 1e-4f);
    }

    uint32_t sr = 0, br = 0;
    uint8_t ch = 0;
    assert(decoder.get_frame_info(sr, ch, br));
    assert(sr == 44100);
    assert(ch == 2);
    assert(br == 128);

    std::cout << "MP3 Decoder facade tests passed!\n";
    return 0;
}
