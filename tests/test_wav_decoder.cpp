// tests/test_wav_decoder.cpp
#include "audio_codecs/wav/wav_decoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::wav;

    // Create 44-byte WAV header + 8 bytes of 16-bit stereo PCM (2 sample frames)
    uint8_t stream[44 + 8] = {
        'R', 'I', 'F', 'F',
        44, 0, 0, 0,        // 36 + 8
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,
        1, 0,
        2, 0,
        0x80, 0xBB, 0, 0,   // 48000 Hz
        0x00, 0xEE, 0x02, 0,// 48000 * 4 = 192000 Bps
        4, 0,
        16, 0,
        'd', 'a', 't', 'a',
        8, 0, 0, 0,
        // Frame 0: Left 0, Right 16384 (+0.5f)
        0x00, 0x00, 0x00, 0x40,
        // Frame 1: Left -32768 (-1.0f), Right 0
        0x00, 0x80, 0x00, 0x00
    };

    WavDecoder decoder;
    size_t consumed = 0;
    bool parsed = decoder.parse_stream_header(stream, sizeof(stream), consumed);
    assert(parsed);
    assert(consumed == 44);
    assert(decoder.get_sample_rate() == 48000);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_bit_depth() == 16);
    assert(decoder.get_format_tag() == WavFormat::Pcm);
    assert(decoder.get_total_samples() == 2);

    // 1. Decode polymorphic float
    float out_pcm[8] = {0};
    int decoded_samples = decoder.decode_frame(stream + 44, 8, out_pcm, 8);
    assert(decoded_samples == 4); // 4 interleaved samples (2 frames * 2 channels)
    assert(out_pcm[0] == 0.0f);
    assert(std::abs(out_pcm[1] - 0.5f) < 1e-4f);
    assert(out_pcm[2] == -1.0f);
    assert(out_pcm[3] == 0.0f);
    assert(decoder.get_last_frame_bytes() == 8);

    // 2. Decode int16
    int16_t out_i16[8] = {0};
    decoded_samples = decoder.decode_frame_i16(stream + 44, 8, out_i16, 8);
    assert(decoded_samples == 4);
    assert(out_i16[0] == 0);
    assert(out_i16[1] == 16384);
    assert(out_i16[2] == -32768);
    assert(out_i16[3] == 0);

    // 3. Decode int32
    int32_t out_i32[8] = {0};
    decoded_samples = decoder.decode_frame_i32(stream + 44, 8, out_i32, 8);
    assert(decoded_samples == 4);
    assert(out_i32[0] == 0);
    assert(out_i32[1] == (16384 << 16));
    assert(out_i32[2] == static_cast<int32_t>(-32768 * 65536LL));
    assert(out_i32[3] == 0);

    std::cout << "WAV decoder test passed!\n";
    return 0;
}
