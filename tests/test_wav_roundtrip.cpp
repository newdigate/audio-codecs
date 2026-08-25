// tests/test_wav_roundtrip.cpp
#include "audio_codecs/wav/wav_decoder.h"
#include "audio_codecs/wav/wav_encoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_format_roundtrip_float(audio_codecs::wav::WavSampleFormat fmt, uint32_t sample_rate, uint8_t channels) {
    using namespace audio_codecs::wav;

    WavEncoderBase<8> encoder;
    WavEncoderConfig enc_cfg;
    enc_cfg.core_config.sample_rate = sample_rate;
    enc_cfg.core_config.channels = channels;
    enc_cfg.sample_format = fmt;
    assert(encoder.init_wav(enc_cfg));

    std::vector<uint8_t> stream(256);
    int hdr_len = encoder.write_stream_header(stream.data(), stream.size());
    assert(hdr_len > 0);

    const size_t num_frames = 100;
    const size_t total_samples = num_frames * channels;
    std::vector<float> orig_pcm(total_samples);
    for (size_t i = 0; i < total_samples; ++i) {
        orig_pcm[i] = std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i / channels)) / static_cast<float>(sample_rate)) * 0.8f;
    }

    std::vector<uint8_t> payload(total_samples * 8);
    int enc_bytes = encoder.encode_frame(orig_pcm.data(), total_samples, payload.data(), payload.size());
    assert(enc_bytes > 0);

    encoder.finalize_header(stream.data(), static_cast<uint32_t>(enc_bytes));
    stream.resize(hdr_len);
    stream.insert(stream.end(), payload.begin(), payload.begin() + enc_bytes);

    // Decode
    WavDecoderBase<8> decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(stream.data(), stream.size(), consumed));
    assert(decoder.get_sample_rate() == sample_rate);
    assert(decoder.get_channels() == channels);

    std::vector<float> dec_pcm(total_samples);
    int dec_samples = decoder.decode_frame(stream.data() + consumed, stream.size() - consumed, dec_pcm.data(), dec_pcm.size());
    assert(dec_samples == static_cast<int>(total_samples));

    // Compute max absolute difference
    float max_diff = 0.0f;
    for (size_t i = 0; i < total_samples; ++i) {
        float diff = std::abs(orig_pcm[i] - dec_pcm[i]);
        if (diff > max_diff) max_diff = diff;
    }

    if (fmt == WavSampleFormat::Float32LE) {
        assert(max_diff < 1e-6f);
    } else if (fmt == WavSampleFormat::Int32LE || fmt == WavSampleFormat::Int24LE) {
        assert(max_diff < 1e-4f);
    } else if (fmt == WavSampleFormat::Int16LE) {
        assert(max_diff < 1e-3f);
    } else {
        assert(max_diff < 0.05f); // 8-bit / G.711 quantization error
    }
}

void test_bit_exact_i16_roundtrip() {
    using namespace audio_codecs::wav;

    WavEncoder encoder;
    WavEncoderConfig enc_cfg;
    enc_cfg.core_config.sample_rate = 44100;
    enc_cfg.core_config.channels = 2;
    enc_cfg.sample_format = WavSampleFormat::Int16LE;
    assert(encoder.init_wav(enc_cfg));

    std::vector<uint8_t> stream(44);
    int hdr_len = encoder.write_stream_header(stream.data(), stream.size());
    assert(hdr_len == 44);

    std::vector<int16_t> orig_i16 = {-32768, -16384, -1, 0, 1, 16384, 32767, -100};
    std::vector<uint8_t> payload(orig_i16.size() * 2);
    int enc_bytes = encoder.encode_frame_i16(orig_i16.data(), orig_i16.size(), payload.data(), payload.size());
    assert(enc_bytes == static_cast<int>(orig_i16.size() * 2));

    encoder.finalize_header(stream.data(), static_cast<uint32_t>(enc_bytes));
    stream.insert(stream.end(), payload.begin(), payload.end());

    WavDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(stream.data(), stream.size(), consumed));
    assert(consumed == 44);

    std::vector<int16_t> dec_i16(orig_i16.size());
    int dec_samples = decoder.decode_frame_i16(stream.data() + consumed, stream.size() - consumed, dec_i16.data(), dec_i16.size());
    assert(dec_samples == static_cast<int>(orig_i16.size()));

    for (size_t i = 0; i < orig_i16.size(); ++i) {
        assert(orig_i16[i] == dec_i16[i]);
    }
}

void test_bit_exact_i32_roundtrip() {
    using namespace audio_codecs::wav;

    WavEncoder encoder;
    WavEncoderConfig enc_cfg;
    enc_cfg.core_config.sample_rate = 48000;
    enc_cfg.core_config.channels = 2;
    enc_cfg.sample_format = WavSampleFormat::Int32LE;
    assert(encoder.init_wav(enc_cfg));

    std::vector<uint8_t> stream(44);
    int hdr_len = encoder.write_stream_header(stream.data(), stream.size());
    assert(hdr_len == 44);

    std::vector<int32_t> orig_i32 = {-2147483647 - 1, -1000000, -1, 0, 1, 1000000, 2147483647, -42};
    std::vector<uint8_t> payload(orig_i32.size() * 4);
    int enc_bytes = encoder.encode_frame_i32(orig_i32.data(), orig_i32.size(), payload.data(), payload.size());
    assert(enc_bytes == static_cast<int>(orig_i32.size() * 4));

    encoder.finalize_header(stream.data(), static_cast<uint32_t>(enc_bytes));
    stream.insert(stream.end(), payload.begin(), payload.end());

    WavDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(stream.data(), stream.size(), consumed));
    assert(consumed == 44);

    std::vector<int32_t> dec_i32(orig_i32.size());
    int dec_samples = decoder.decode_frame_i32(stream.data() + consumed, stream.size() - consumed, dec_i32.data(), dec_i32.size());
    assert(dec_samples == static_cast<int>(orig_i32.size()));

    for (size_t i = 0; i < orig_i32.size(); ++i) {
        assert(orig_i32[i] == dec_i32[i]);
    }
}

int main() {
    using namespace audio_codecs::wav;

    std::cout << "Testing 16-bit Stereo PCM...\n";
    test_format_roundtrip_float(WavSampleFormat::Int16LE, 44100, 2);

    std::cout << "Testing 24-bit 96kHz PCM...\n";
    test_format_roundtrip_float(WavSampleFormat::Int24LE, 96000, 2);

    std::cout << "Testing 32-bit PCM...\n";
    test_format_roundtrip_float(WavSampleFormat::Int32LE, 48000, 2);

    std::cout << "Testing 32-bit IEEE Float...\n";
    test_format_roundtrip_float(WavSampleFormat::Float32LE, 48000, 2);

    std::cout << "Testing 8-bit unsigned PCM...\n";
    test_format_roundtrip_float(WavSampleFormat::Uint8, 22050, 1);

    std::cout << "Testing 8-bit A-law...\n";
    test_format_roundtrip_float(WavSampleFormat::ALaw8, 8000, 1);

    std::cout << "Testing 8-bit mu-law...\n";
    test_format_roundtrip_float(WavSampleFormat::MuLaw8, 8000, 1);

    std::cout << "Testing 5.1 Surround 24-bit (Extensible)...\n";
    test_format_roundtrip_float(WavSampleFormat::Int24LE, 48000, 6);

    std::cout << "Testing bit-exact 16-bit integer roundtrip (diff = 0)...\n";
    test_bit_exact_i16_roundtrip();

    std::cout << "Testing bit-exact 32-bit integer roundtrip (diff = 0)...\n";
    test_bit_exact_i32_roundtrip();

    std::cout << "All WAV roundtrip tests passed successfully!\n";
    return 0;
}
