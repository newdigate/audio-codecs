// tests/test_aiff_encoder.cpp
#include "audio_codecs/aiff/aiff_encoder.h"
#include "audio_codecs/aiff/aiff_decoder.h"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 48000;
    cfg.core_config.channels = 2;
    cfg.sample_format = AiffSampleFormat::Int16BE;
    encoder.init_aiff(cfg);

    uint8_t header[128];
    int hdr_bytes = encoder.write_stream_header(header, sizeof(header));
    assert(hdr_bytes > 0);

    int16_t pcm[200];
    for (int i = 0; i < 100; ++i) {
        pcm[i * 2] = static_cast<int16_t>(i * 50);
        pcm[i * 2 + 1] = static_cast<int16_t>(-i * 50);
    }

    uint8_t audio_out[512];
    int enc_bytes = encoder.encode_frame_i16(pcm, 200, audio_out, sizeof(audio_out));
    assert(enc_bytes == 400);

    encoder.finalize_header(header, static_cast<uint32_t>(enc_bytes));

    // Decode back
    std::vector<uint8_t> full_file(header, header + hdr_bytes);
    full_file.insert(full_file.end(), audio_out, audio_out + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    bool ok = decoder.parse_stream_header(full_file.data(), full_file.size(), consumed);
    assert(ok);
    assert(decoder.get_sample_rate() == 48000);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_total_frames() == 100);
    assert(decoder.get_sample_format() == AiffSampleFormat::Int16BE);

    int16_t dec_pcm[200];
    int dec_samples = decoder.decode_frame_i16(full_file.data() + consumed, full_file.size() - consumed, dec_pcm, 200);
    assert(dec_samples == 200);
    for (int i = 0; i < 200; ++i) {
        assert(dec_pcm[i] == pcm[i]);
    }

    // Test AIFC sowt encoder
    AiffEncoder aifc_enc;
    AiffEncoderConfig aifc_cfg;
    aifc_cfg.core_config.sample_rate = 44100;
    aifc_cfg.core_config.channels = 1;
    aifc_cfg.form_type = AiffFormType::Aifc;
    aifc_cfg.compression_type = AiffCompressionType::Sowt;
    aifc_cfg.sample_format = AiffSampleFormat::Int16LE;
    aifc_enc.init_aiff(aifc_cfg);

    uint8_t aifc_hdr[128];
    int aifc_hdr_len = aifc_enc.write_stream_header(aifc_hdr, sizeof(aifc_hdr));
    assert(aifc_hdr_len > 0);

    int16_t mono_pcm[50];
    for (int i = 0; i < 50; ++i) {
        mono_pcm[i] = static_cast<int16_t>(i * 123);
    }
    uint8_t aifc_audio[128];
    int aifc_enc_bytes = aifc_enc.encode_frame_i16(mono_pcm, 50, aifc_audio, sizeof(aifc_audio));
    assert(aifc_enc_bytes == 100);

    aifc_enc.finalize_header(aifc_hdr, static_cast<uint32_t>(aifc_enc_bytes));

    std::vector<uint8_t> aifc_file(aifc_hdr, aifc_hdr + aifc_hdr_len);
    aifc_file.insert(aifc_file.end(), aifc_audio, aifc_audio + aifc_enc_bytes);

    AiffDecoder aifc_dec;
    consumed = 0;
    assert(aifc_dec.parse_stream_header(aifc_file.data(), aifc_file.size(), consumed));
    assert(aifc_dec.get_form_type() == AiffFormType::Aifc);
    assert(aifc_dec.get_compression_type() == AiffCompressionType::Sowt);
    assert(aifc_dec.get_sample_format() == AiffSampleFormat::Int16LE);
    assert(aifc_dec.get_channels() == 1);
    assert(aifc_dec.get_total_frames() == 50);

    int16_t dec_mono[50];
    int dec_mono_samples = aifc_dec.decode_frame_i16(aifc_file.data() + consumed, aifc_file.size() - consumed, dec_mono, 50);
    assert(dec_mono_samples == 50);
    for (int i = 0; i < 50; ++i) {
        assert(dec_mono[i] == mono_pcm[i]);
    }

    std::cout << "AIFF encoder test passed!\n";
    return 0;
}
