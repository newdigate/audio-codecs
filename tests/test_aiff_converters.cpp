// tests/test_aiff_converters.cpp
#include "src/aiff/sample_converter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::aiff;

    assert(bytes_per_sample(AiffSampleFormat::Int8) == 1);
    assert(bytes_per_sample(AiffSampleFormat::ALaw8) == 1);
    assert(bytes_per_sample(AiffSampleFormat::MuLaw8) == 1);
    assert(bytes_per_sample(AiffSampleFormat::Int16BE) == 2);
    assert(bytes_per_sample(AiffSampleFormat::Int16LE) == 2);
    assert(bytes_per_sample(AiffSampleFormat::Int24BE) == 3);
    assert(bytes_per_sample(AiffSampleFormat::Int24LE) == 3);
    assert(bytes_per_sample(AiffSampleFormat::Int32BE) == 4);
    assert(bytes_per_sample(AiffSampleFormat::Int32LE) == 4);
    assert(bytes_per_sample(AiffSampleFormat::Float32BE) == 4);
    assert(bytes_per_sample(AiffSampleFormat::Float32LE) == 4);

    // Test 16-bit BE PCM conversions
    int16_t src16[4] = { -32768, -1, 0, 32767 };
    uint8_t enc16[8];
    encode_samples_from_i16(src16, AiffSampleFormat::Int16BE, enc16, 4);
    assert(enc16[0] == 0x80 && enc16[1] == 0x00);
    assert(enc16[2] == 0xFF && enc16[3] == 0xFF);
    assert(enc16[4] == 0x00 && enc16[5] == 0x00);
    assert(enc16[6] == 0x7F && enc16[7] == 0xFF);

    int16_t dec16[4];
    decode_samples_to_i16(enc16, AiffSampleFormat::Int16BE, dec16, 4);
    for (int i = 0; i < 4; ++i) {
        assert(dec16[i] == src16[i]);
    }

    // Test 8-bit signed PCM conversions (two's complement: -128, -1, 0, 127)
    uint8_t enc8[4];
    int16_t src8_as_16[4] = { -32768, -256, 0, 32512 };
    encode_samples_from_i16(src8_as_16, AiffSampleFormat::Int8, enc8, 4);
    assert(static_cast<int8_t>(enc8[0]) == -128);
    assert(static_cast<int8_t>(enc8[1]) == -1);
    assert(static_cast<int8_t>(enc8[2]) == 0);
    assert(static_cast<int8_t>(enc8[3]) == 127);

    int16_t dec8_as_16[4];
    decode_samples_to_i16(enc8, AiffSampleFormat::Int8, dec8_as_16, 4);
    for (int i = 0; i < 4; ++i) {
        assert(dec8_as_16[i] == src8_as_16[i]);
    }

    // Test 24-bit BE PCM
    int32_t src24[4] = { -8388608 * 256, -1 * 256, 0, 8388607 * 256 };
    uint8_t enc24[12];
    encode_samples_from_i32(src24, AiffSampleFormat::Int24BE, enc24, 4);
    assert(enc24[0] == 0x80 && enc24[1] == 0x00 && enc24[2] == 0x00);
    assert(enc24[3] == 0xFF && enc24[4] == 0xFF && enc24[5] == 0xFF);
    assert(enc24[6] == 0x00 && enc24[7] == 0x00 && enc24[8] == 0x00);
    assert(enc24[9] == 0x7F && enc24[10] == 0xFF && enc24[11] == 0xFF);

    int32_t dec24[4];
    decode_samples_to_i32(enc24, AiffSampleFormat::Int24BE, dec24, 4);
    for (int i = 0; i < 4; ++i) {
        assert(dec24[i] == src24[i]);
    }

    // Test 32-bit BE PCM
    int32_t src32[4] = { static_cast<int32_t>(0x80000000), -1, 0, 0x7FFFFFFF };
    uint8_t enc32[16];
    encode_samples_from_i32(src32, AiffSampleFormat::Int32BE, enc32, 4);
    assert(enc32[0] == 0x80 && enc32[1] == 0x00 && enc32[2] == 0x00 && enc32[3] == 0x00);
    assert(enc32[4] == 0xFF && enc32[5] == 0xFF && enc32[6] == 0xFF && enc32[7] == 0xFF);
    assert(enc32[8] == 0x00 && enc32[9] == 0x00 && enc32[10] == 0x00 && enc32[11] == 0x00);
    assert(enc32[12] == 0x7F && enc32[13] == 0xFF && enc32[14] == 0xFF && enc32[15] == 0xFF);

    int32_t dec32[4];
    decode_samples_to_i32(enc32, AiffSampleFormat::Int32BE, dec32, 4);
    for (int i = 0; i < 4; ++i) {
        assert(dec32[i] == src32[i]);
    }

    // Test sowt (16-bit LE)
    uint8_t enc_sowt[8];
    encode_samples_from_i16(src16, AiffSampleFormat::Int16LE, enc_sowt, 4);
    assert(enc_sowt[0] == 0x00 && enc_sowt[1] == 0x80);
    int16_t dec_sowt[4];
    decode_samples_to_i16(enc_sowt, AiffSampleFormat::Int16LE, dec_sowt, 4);
    for (int i = 0; i < 4; ++i) {
        assert(dec_sowt[i] == src16[i]);
    }

    // Test Float32 BE
    float src_f[4] = { -1.0f, -0.5f, 0.0f, 1.0f };
    uint8_t enc_f[16];
    encode_samples_from_float(src_f, AiffSampleFormat::Float32BE, enc_f, 4);
    float dec_f[4];
    decode_samples_to_float(enc_f, AiffSampleFormat::Float32BE, dec_f, 4);
    for (int i = 0; i < 4; ++i) {
        assert(std::abs(dec_f[i] - src_f[i]) < 1e-6f);
    }

    // Test G.711 ALaw & MuLaw
    int16_t g711_src[4] = { -30000, -1000, 0, 30000 };
    uint8_t enc_alaw[4];
    encode_samples_from_i16(g711_src, AiffSampleFormat::ALaw8, enc_alaw, 4);
    int16_t dec_alaw[4];
    decode_samples_to_i16(enc_alaw, AiffSampleFormat::ALaw8, dec_alaw, 4);
    assert(dec_alaw[0] < -20000 && dec_alaw[1] < 0 && std::abs(dec_alaw[2]) <= 8 && dec_alaw[3] > 20000);

    uint8_t enc_mulaw[4];
    encode_samples_from_i16(g711_src, AiffSampleFormat::MuLaw8, enc_mulaw, 4);
    int16_t dec_mulaw[4];
    decode_samples_to_i16(enc_mulaw, AiffSampleFormat::MuLaw8, dec_mulaw, 4);
    assert(dec_mulaw[0] < -20000 && dec_mulaw[1] < 0 && dec_mulaw[2] == 0 && dec_mulaw[3] > 20000);

    std::cout << "AIFF converter tests passed!\n";
    return 0;
}
