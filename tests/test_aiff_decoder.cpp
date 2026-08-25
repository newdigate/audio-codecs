// tests/test_aiff_decoder.cpp
#include "audio_codecs/aiff/aiff_decoder.h"
#include "src/aiff/ieee80.h"
#include "src/aiff/aiff_common.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    // Create 100 stereo 16-bit BE samples
    std::vector<uint8_t> stream;
    // FORM header
    stream.insert(stream.end(), {'F', 'O', 'R', 'M'});
    uint32_t form_size = 4 + (8 + 18) + (8 + 8 + 400);
    uint8_t sz[4];
    write_be32(sz, form_size);
    stream.insert(stream.end(), sz, sz + 4);
    stream.insert(stream.end(), {'A', 'I', 'F', 'F'});

    // COMM chunk
    stream.insert(stream.end(), {'C', 'O', 'M', 'M'});
    write_be32(sz, 18);
    stream.insert(stream.end(), sz, sz + 4);
    uint8_t comm_data[18];
    write_be16(comm_data, 2);
    write_be32(comm_data + 2, 100);
    write_be16(comm_data + 6, 16);
    uint32_to_ieee80(44100, comm_data + 8);
    stream.insert(stream.end(), comm_data, comm_data + 18);

    // SSND chunk
    stream.insert(stream.end(), {'S', 'S', 'N', 'D'});
    write_be32(sz, 408);
    stream.insert(stream.end(), sz, sz + 4);
    uint8_t ssnd_hdr[8] = {0};
    stream.insert(stream.end(), ssnd_hdr, ssnd_hdr + 8);

    // Raw samples (100 frames of stereo 16-bit = 200 samples = 400 bytes)
    for (int i = 0; i < 100; ++i) {
        int16_t l = static_cast<int16_t>(i * 100);
        int16_t r = static_cast<int16_t>(-i * 100);
        uint8_t b[4];
        write_be16(b, static_cast<uint16_t>(l));
        write_be16(b + 2, static_cast<uint16_t>(r));
        stream.insert(stream.end(), b, b + 4);
    }

    AiffDecoder decoder;
    size_t consumed = 0;
    bool ok = decoder.parse_stream_header(stream.data(), stream.size(), consumed);
    assert(ok);
    assert(decoder.get_sample_rate() == 44100);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_bit_depth() == 16);
    assert(decoder.get_total_frames() == 100);
    assert(decoder.get_form_type() == AiffFormType::Aiff);
    assert(decoder.get_sample_format() == AiffSampleFormat::Int16BE);

    // Test direct integer 16-bit decode
    int16_t out_i16[200];
    int samples_i16 = decoder.decode_frame_i16(stream.data() + consumed, stream.size() - consumed, out_i16, 200);
    assert(samples_i16 == 200);
    for (int i = 0; i < 100; ++i) {
        assert(out_i16[i * 2] == static_cast<int16_t>(i * 100));
        assert(out_i16[i * 2 + 1] == static_cast<int16_t>(-i * 100));
    }

    // Test normalized float decode
    float out_f[200];
    int samples_f = decoder.decode_frame(stream.data() + consumed, stream.size() - consumed, out_f, 200);
    assert(samples_f == 200);
    for (int i = 0; i < 100; ++i) {
        float expected_l = static_cast<float>(i * 100) / 32768.0f;
        float expected_r = static_cast<float>(-i * 100) / 32768.0f;
        assert(std::abs(out_f[i * 2] - expected_l) < 1e-5f);
        assert(std::abs(out_f[i * 2 + 1] - expected_r) < 1e-5f);
    }

    // Test direct integer 32-bit decode
    int32_t out_i32[200];
    int samples_i32 = decoder.decode_frame_i32(stream.data() + consumed, stream.size() - consumed, out_i32, 200);
    assert(samples_i32 == 200);
    for (int i = 0; i < 100; ++i) {
        assert(out_i32[i * 2] == (static_cast<int32_t>(i * 100) << 16));
        assert(out_i32[i * 2 + 1] == (static_cast<int32_t>(-i * 100) << 16));
    }

    std::cout << "AIFF decoder test passed!\n";
    return 0;
}
