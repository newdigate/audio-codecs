// tests/test_wav_converters.cpp
#include "src/wav/sample_converter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::wav;

    assert(bytes_per_sample(WavSampleFormat::Uint8) == 1);
    assert(bytes_per_sample(WavSampleFormat::Int16LE) == 2);
    assert(bytes_per_sample(WavSampleFormat::Int24LE) == 3);
    assert(bytes_per_sample(WavSampleFormat::Int32LE) == 4);
    assert(bytes_per_sample(WavSampleFormat::Float32LE) == 4);
    assert(bytes_per_sample(WavSampleFormat::ALaw8) == 1);
    assert(bytes_per_sample(WavSampleFormat::MuLaw8) == 1);

    // 1. Test 16-bit PCM roundtrip
    int16_t s16_in[4] = {-32768, -1, 0, 32767};
    uint8_t raw16[8];
    encode_samples_from_i16(s16_in, WavSampleFormat::Int16LE, raw16, 4);
    int16_t s16_out[4];
    decode_samples_to_i16(raw16, WavSampleFormat::Int16LE, s16_out, 4);
    for (int i = 0; i < 4; ++i) assert(s16_in[i] == s16_out[i]);

    // 2. Test 24-bit PCM roundtrip
    int32_t s24_in[4] = {-8388608, -1, 0, 8388607};
    uint8_t raw24[12];
    encode_samples_from_i32(s24_in, WavSampleFormat::Int24LE, raw24, 4);
    int32_t s24_out[4];
    decode_samples_to_i32(raw24, WavSampleFormat::Int24LE, s24_out, 4);
    for (int i = 0; i < 4; ++i) assert(s24_in[i] == s24_out[i]);

    // 3. Test 8-bit unsigned roundtrip
    uint8_t u8_in[4] = {0, 128, 200, 255};
    float f_out[4];
    decode_samples_to_float(u8_in, WavSampleFormat::Uint8, f_out, 4);
    assert(std::abs(f_out[1] - 0.0f) < 1e-5f);
    assert(f_out[0] == -1.0f);
    uint8_t u8_out[4];
    encode_samples_from_float(f_out, WavSampleFormat::Uint8, u8_out, 4);
    for (int i = 0; i < 4; ++i) assert(u8_in[i] == u8_out[i]);

    // 4. Test 32-bit Float roundtrip
    float f32_in[4] = {-1.0f, -0.5f, 0.0f, 0.75f};
    uint8_t raw_f32[16];
    encode_samples_from_float(f32_in, WavSampleFormat::Float32LE, raw_f32, 4);
    float f32_out[4];
    decode_samples_to_float(raw_f32, WavSampleFormat::Float32LE, f32_out, 4);
    for (int i = 0; i < 4; ++i) assert(f32_in[i] == f32_out[i]);

    // 5. Test 32-bit PCM roundtrip
    int32_t s32_in[4] = {-2147483647 - 1, -1000, 0, 2147483647};
    uint8_t raw32[16];
    encode_samples_from_i32(s32_in, WavSampleFormat::Int32LE, raw32, 4);
    int32_t s32_out[4];
    decode_samples_to_i32(raw32, WavSampleFormat::Int32LE, s32_out, 4);
    for (int i = 0; i < 4; ++i) assert(s32_in[i] == s32_out[i]);

    std::cout << "WAV sample converters test passed!\n";
    return 0;
}
