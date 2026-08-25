// tests/test_flac_roundtrip.cpp
#include "audio_codecs/flac/flac_encoder.h"
#include "audio_codecs/flac/flac_decoder.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_level(uint8_t compression_level, uint16_t block_size) {
    using namespace audio_codecs::flac;

    FlacEncoder encoder;
    FlacDecoder decoder;

    FlacEncoderConfig cfg;
    cfg.core_config = {44100, 2, 0, false, 2};
    cfg.compression_level = compression_level;
    cfg.block_size = block_size;
    cfg.bit_depth = 16;

    assert(encoder.init_flac(cfg));
    assert(decoder.init(cfg.core_config));

    // Multi-frame test (4 consecutive frames)
    const int num_frames = 4;
    const size_t total_samples = static_cast<size_t>(block_size) * 2 * num_frames;

    std::vector<int16_t> orig_pcm(total_samples);
    for (size_t i = 0; i < total_samples / 2; ++i) {
        double s1 = std::sin(2.0 * audio_codecs::constants::PI * 440.0 * i / 44100.0);
        double s2 = std::cos(2.0 * audio_codecs::constants::PI * 1000.0 * i / 44100.0);
        orig_pcm[i * 2]     = static_cast<int16_t>(16000.0 * s1);
        orig_pcm[i * 2 + 1] = static_cast<int16_t>(16000.0 * s2);
    }

    std::vector<uint8_t> flac_stream;
    uint8_t frame_buf[16384];

    // Encode all frames
    for (int f = 0; f < num_frames; ++f) {
        const int16_t* in_ptr = &orig_pcm[f * block_size * 2];
        int enc_bytes = encoder.encode_frame_i16(in_ptr, block_size * 2, frame_buf, sizeof(frame_buf));
        assert(enc_bytes > 0);
        flac_stream.insert(flac_stream.end(), frame_buf, frame_buf + enc_bytes);
    }

    // Decode all frames
    std::vector<int16_t> dec_pcm(total_samples, 0);
    size_t stream_offset = 0;
    size_t out_offset = 0;

    for (int f = 0; f < num_frames; ++f) {
        int dec_samples = decoder.decode_frame_i16(flac_stream.data() + stream_offset, 
                                                   flac_stream.size() - stream_offset, 
                                                   &dec_pcm[out_offset], 
                                                   block_size * 2);
        assert(dec_samples == block_size * 2);
        out_offset += dec_samples;
        stream_offset += decoder.get_last_frame_bytes();
    }

    // 100% BIT-EXACT LOSSLESS VERIFICATION
    for (size_t i = 0; i < total_samples; ++i) {
        assert(dec_pcm[i] == orig_pcm[i]);
    }

    std::cout << "[PASS] Level " << static_cast<int>(compression_level) 
              << " (BlockSize " << block_size << "): "
              << total_samples / 2 << " stereo samples compressed to " 
              << flac_stream.size() << " bytes (ratio: " 
              << (100.0 * flac_stream.size() / (total_samples * 2)) << "%)\n";
}

int main() {
    // Test Fast Levels (Fixed Predictors)
    test_level(0, 1152);
    test_level(2, 4096);

    // Test Medium & High Levels (LPC Analysis)
    test_level(5, 1152);
    test_level(8, 4096);

    std::cout << "\nAll FLAC Lossless Roundtrip verification tests passed 100% bit-exact!\n";
    return 0;
}
