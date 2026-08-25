// tests/test_aiff_roundtrip.cpp
#include "audio_codecs/aiff.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

using namespace audio_codecs::aiff;

void test_pcm8_signed_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 22050;
    cfg.core_config.channels = 1;
    cfg.form_type = AiffFormType::Aiff;
    cfg.sample_format = AiffSampleFormat::Int8;
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));
    assert(hdr_sz > 0);

    // 256 signed 8-bit samples (-128 .. 127)
    std::vector<int16_t> orig(256);
    for (int i = 0; i < 256; ++i) {
        int8_t s8 = static_cast<int8_t>(i - 128);
        orig[i] = static_cast<int16_t>(s8) << 8;
    }

    std::vector<uint8_t> payload(256);
    int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 256);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_sample_format() == AiffSampleFormat::Int8);
    assert(decoder.get_channels() == 1);
    assert(decoder.get_sample_rate() == 22050);

    std::vector<int16_t> decoded(256);
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 256);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact lossless for signed 8-bit
    }
}

void test_pcm16_be_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 44100;
    cfg.core_config.channels = 2;
    cfg.form_type = AiffFormType::Aiff;
    cfg.sample_format = AiffSampleFormat::Int16BE;
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));
    assert(hdr_sz > 0);

    std::vector<int16_t> orig(2000);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int16_t>(std::sin(i * 0.05) * 32000.0);
    }

    std::vector<uint8_t> payload(4000);
    int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 4000);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_sample_rate() == 44100);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_bit_depth() == 16);

    std::vector<int16_t> decoded(2000);
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 2000);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact lossless
    }
}

void test_pcm24_be_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 96000;
    cfg.core_config.channels = 2;
    cfg.form_type = AiffFormType::Aiff;
    cfg.sample_format = AiffSampleFormat::Int24BE;
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    std::vector<int32_t> orig(1000);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int32_t>(std::sin(i * 0.03) * 8000000.0) * 256;
    }

    std::vector<uint8_t> payload(3000);
    int enc_bytes = encoder.encode_frame_i32(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 3000);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_sample_format() == AiffSampleFormat::Int24BE);

    std::vector<int32_t> decoded(1000);
    int dec_samples = decoder.decode_frame_i32(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 1000);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact lossless
    }
}

void test_pcm32_be_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 48000;
    cfg.core_config.channels = 2;
    cfg.form_type = AiffFormType::Aiff;
    cfg.sample_format = AiffSampleFormat::Int32BE;
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    std::vector<int32_t> orig(500);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int32_t>(std::sin(i * 0.02) * 2000000000.0);
    }

    std::vector<uint8_t> payload(2000);
    int enc_bytes = encoder.encode_frame_i32(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 2000);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_sample_format() == AiffSampleFormat::Int32BE);

    std::vector<int32_t> decoded(500);
    int dec_samples = decoder.decode_frame_i32(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 500);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact lossless
    }
}

void test_aifc_sowt_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 44100;
    cfg.core_config.channels = 2;
    cfg.sample_format = AiffSampleFormat::Int16LE; // sowt
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    std::vector<int16_t> orig(1000);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int16_t>(i * 31);
    }

    std::vector<uint8_t> payload(2000);
    int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_form_type() == AiffFormType::Aifc);
    assert(decoder.get_compression_type() == AiffCompressionType::Sowt);

    std::vector<int16_t> decoded(1000);
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 1000);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact lossless
    }
}

void test_aifc_float32_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 48000;
    cfg.core_config.channels = 2;
    cfg.sample_format = AiffSampleFormat::Float32BE;
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    std::vector<float> orig(500);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<float>(std::sin(i * 0.1) * 0.95);
    }

    std::vector<uint8_t> payload(2000);
    int enc_bytes = encoder.encode_frame(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 2000);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_form_type() == AiffFormType::Aifc);
    assert(decoder.get_compression_type() == AiffCompressionType::Fl32);

    std::vector<float> decoded(500);
    int dec_samples = decoder.decode_frame(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 500);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(std::abs(decoded[i] - orig[i]) < 1e-6f); // Exact float match
    }
}

void test_aifc_g711_roundtrip() {
    // A-law
    {
        AiffEncoder encoder;
        AiffEncoderConfig cfg;
        cfg.core_config.sample_rate = 8000;
        cfg.core_config.channels = 1;
        cfg.sample_format = AiffSampleFormat::ALaw8;
        assert(encoder.init_aiff(cfg));

        uint8_t hdr[128];
        int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

        std::vector<int16_t> orig(256);
        for (int i = 0; i < 256; ++i) {
            orig[i] = static_cast<int16_t>((i - 128) * 200);
        }

        std::vector<uint8_t> payload(256);
        int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
        encoder.finalize_header(hdr, enc_bytes);

        std::vector<uint8_t> full(hdr, hdr + hdr_sz);
        full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

        AiffDecoder decoder;
        size_t consumed = 0;
        assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
        assert(decoder.get_compression_type() == AiffCompressionType::ALaw);

        std::vector<int16_t> decoded(256);
        int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
        assert(dec_samples == 256);
    }

    // mu-law
    {
        AiffEncoder encoder;
        AiffEncoderConfig cfg;
        cfg.core_config.sample_rate = 8000;
        cfg.core_config.channels = 1;
        cfg.sample_format = AiffSampleFormat::MuLaw8;
        assert(encoder.init_aiff(cfg));

        uint8_t hdr[128];
        int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

        std::vector<int16_t> orig(256);
        for (int i = 0; i < 256; ++i) {
            orig[i] = static_cast<int16_t>((i - 128) * 200);
        }

        std::vector<uint8_t> payload(256);
        int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
        encoder.finalize_header(hdr, enc_bytes);

        std::vector<uint8_t> full(hdr, hdr + hdr_sz);
        full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

        AiffDecoder decoder;
        size_t consumed = 0;
        assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
        assert(decoder.get_compression_type() == AiffCompressionType::MuLaw);

        std::vector<int16_t> decoded(256);
        int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
        assert(dec_samples == 256);
    }
}

void test_multichannel_5_1_roundtrip() {
    AiffEncoderBase<8> encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 48000;
    cfg.core_config.channels = 6; // 5.1 surround
    cfg.form_type = AiffFormType::Aiff;
    cfg.sample_format = AiffSampleFormat::Int16BE;
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    // 100 frames of 6 channels = 600 samples
    std::vector<int16_t> orig(600);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int16_t>(i * 53);
    }

    std::vector<uint8_t> payload(1200);
    int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 1200);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoderBase<8> decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_channels() == 6);
    assert(decoder.get_total_frames() == 100);

    std::vector<int16_t> decoded(600);
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 600);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact multi-channel
    }
}

void test_odd_byte_flush_padding() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 8000;
    cfg.core_config.channels = 1;
    cfg.form_type = AiffFormType::Aiff;
    cfg.sample_format = AiffSampleFormat::Int8; // 1 byte per sample
    assert(encoder.init_aiff(cfg));

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    // Encode 7 samples (7 bytes - odd!)
    int16_t odd_pcm[7] = { 0, 256, 512, 768, 1024, 1280, 1536 };
    uint8_t audio[16];
    int enc_bytes = encoder.encode_frame_i16(odd_pcm, 7, audio, sizeof(audio));
    assert(enc_bytes == 7);

    uint8_t pad_byte[2];
    int flush_bytes = encoder.flush(pad_byte, sizeof(pad_byte));
    assert(flush_bytes == 1); // Emitted 1 pad byte
    assert(pad_byte[0] == 0);

    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), audio, audio + enc_bytes);
    full.push_back(pad_byte[0]);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_total_frames() == 7);

    int16_t decoded[7];
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded, 7);
    assert(dec_samples == 7);
    for (int i = 0; i < 7; ++i) {
        assert(decoded[i] == odd_pcm[i]);
    }
}

int main() {
    test_pcm8_signed_roundtrip();
    test_pcm16_be_roundtrip();
    test_pcm24_be_roundtrip();
    test_pcm32_be_roundtrip();
    test_aifc_sowt_roundtrip();
    test_aifc_float32_roundtrip();
    test_aifc_g711_roundtrip();
    test_multichannel_5_1_roundtrip();
    test_odd_byte_flush_padding();

    std::cout << "All AIFF lossless, float, G.711 and multi-channel roundtrip tests passed!\n";
    return 0;
}
