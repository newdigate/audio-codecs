// tests/test_wav_encoder.cpp
#include "audio_codecs/wav/wav_encoder.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::wav;

    // 1. Test 16-bit Stereo Standard Header
    {
        WavEncoder encoder;
        WavEncoderConfig config;
        config.core_config.sample_rate = 44100;
        config.core_config.channels = 2;
        config.sample_format = WavSampleFormat::Int16LE;
        bool ok = encoder.init_wav(config);
        assert(ok);

        uint8_t header_buf[128] = {0};
        int header_bytes = encoder.write_stream_header(header_buf, sizeof(header_buf));
        assert(header_bytes == 44);

        float in_pcm[4] = {0.0f, 0.5f, -1.0f, 0.0f};
        uint8_t data_buf[128] = {0};
        int encoded_bytes = encoder.encode_frame(in_pcm, 4, data_buf, sizeof(data_buf));
        assert(encoded_bytes == 8); // 4 samples * 2 bytes = 8 bytes

        // Check encoded samples
        int16_t s0 = *reinterpret_cast<int16_t*>(&data_buf[0]);
        int16_t s1 = *reinterpret_cast<int16_t*>(&data_buf[2]);
        int16_t s2 = *reinterpret_cast<int16_t*>(&data_buf[4]);
        int16_t s3 = *reinterpret_cast<int16_t*>(&data_buf[6]);
        assert(s0 == 0);
        assert(s1 == 16384);
        assert(s2 == -32768);
        assert(s3 == 0);

        // Finalize header
        int fin = encoder.finalize_header(header_buf, encoded_bytes);
        assert(fin > 0);
        uint32_t riff_size = *reinterpret_cast<uint32_t*>(&header_buf[4]);
        uint32_t data_size = *reinterpret_cast<uint32_t*>(&header_buf[40]);
        assert(riff_size == 36 + 8);
        assert(data_size == 8);
    }

    // 2. Test IEEE Float with Fact Chunk
    {
        WavEncoder encoder;
        WavEncoderConfig config;
        config.core_config.sample_rate = 48000;
        config.core_config.channels = 1;
        config.sample_format = WavSampleFormat::Float32LE;
        assert(encoder.init_wav(config));

        uint8_t header_buf[128] = {0};
        int header_bytes = encoder.write_stream_header(header_buf, sizeof(header_buf));
        assert(header_bytes == 56); // 12 + 24 (fmt) + 12 (fact) + 8 (data) = 56

        float in_pcm[2] = {-0.5f, 0.75f};
        uint8_t data_buf[64] = {0};
        int encoded_bytes = encoder.encode_frame_f32(in_pcm, 2, data_buf, sizeof(data_buf));
        assert(encoded_bytes == 8); // 2 samples * 4 bytes = 8 bytes

        encoder.finalize_header(header_buf, encoded_bytes);
        uint32_t fact_samples = *reinterpret_cast<uint32_t*>(&header_buf[44]);
        assert(fact_samples == 2);
    }

    // 3. Test Extensible Format for 5.1 Surround
    {
        WavEncoderBase<8> encoder;
        WavEncoderConfig config;
        config.core_config.sample_rate = 96000;
        config.core_config.channels = 6;
        config.sample_format = WavSampleFormat::Int24LE;
        assert(encoder.init_wav(config));

        uint8_t header_buf[128] = {0};
        int header_bytes = encoder.write_stream_header(header_buf, sizeof(header_buf));
        assert(header_bytes == 68); // 12 + 48 (fmt extensible) + 8 (data) = 68
    }

    std::cout << "WAV encoder test passed!\n";
    return 0;
}
