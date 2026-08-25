#include "include/audio_codecs/aac/aac_encoder.h"
#include "include/audio_codecs/aac/adts_header.h"
#include "src/aac/adts_parser.h"
#include "src/aac/aac_tables.h"
#include "src/core/bit_reader.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace audio_codecs;
using namespace audio_codecs::aac;

void test_encoder_lifecycle_and_validation() {
    std::cout << "Testing AacEncoder lifecycle and validation...\n";
    AacEncoder encoder;

    // Default init: 44100 Hz, 2 channels, 128 kbps
    AudioConfig config{44100, 2, 128, false, 4};
    assert(encoder.init(config));
    encoder.reset();

    // Invalid configurations
    AudioConfig invalid_ch{44100, 0, 128, false, 4};
    assert(!encoder.init(invalid_ch));

    AudioConfig invalid_ch3{44100, 3, 128, false, 4};
    assert(!encoder.init(invalid_ch3));

    AudioConfig invalid_sr{12345, 2, 128, false, 4};
    assert(!encoder.init(invalid_sr));

    // Valid sample rates
    uint32_t valid_rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000};
    for (uint32_t sr : valid_rates) {
        AudioConfig valid_cfg{sr, 2, 128, false, 4};
        assert(encoder.init(valid_cfg));
    }

    // Valid channel modes (Mono and Stereo)
    AudioConfig mono_cfg{44100, 1, 64, false, 4};
    assert(encoder.init(mono_cfg));

    AudioConfig stereo_cfg{48000, 2, 192, false, 4};
    assert(encoder.init(stereo_cfg));

    // Error handling on encode_frame
    float pcm[2048] = {0.0f};
    uint8_t out_buf[1024] = {0};

    // Null pointers
    assert(encoder.encode_frame(nullptr, 2048, out_buf, sizeof(out_buf)) < 0);
    assert(encoder.encode_frame(pcm, 2048, nullptr, sizeof(out_buf)) < 0);

    // Insufficient input samples
    assert(encoder.encode_frame(pcm, 500, out_buf, sizeof(out_buf)) < 0);

    // Insufficient output buffer
    assert(encoder.encode_frame(pcm, 2048, out_buf, 4) < 0);

    // Flush test
    assert(encoder.flush(out_buf, sizeof(out_buf)) >= 0);
}

void test_encode_mono_adts_frames() {
    std::cout << "Testing mono ADTS frame encoding...\n";
    AacEncoder encoder;
    AudioConfig config{44100, 1, 64, false, 4};
    assert(encoder.init(config));

    std::vector<float> pcm(1024);
    for (size_t i = 0; i < 1024; ++i) {
        pcm[i] = 0.6f * std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
    }

    uint8_t out_buf[2048] = {0};

    // Encode 5 consecutive frames
    for (int f = 0; f < 5; ++f) {
        int bytes = encoder.encode_frame(pcm.data(), pcm.size(), out_buf, sizeof(out_buf));
        assert(bytes > 7);

        // Verify ADTS header
        core::BitReader reader;
        reader.init(out_buf, bytes);
        AdtsHeader header;
        assert(parse_adts_header(reader, header));

        assert(header.syncword == 0xFFF);
        assert(header.sample_rate == 44100);
        assert(header.channel_configuration == 1);
        assert(header.frame_length == bytes);
        assert(header.profile == 1); // AAC-LC
    }
}

void test_encode_stereo_adts_frames() {
    std::cout << "Testing stereo ADTS frame encoding...\n";
    AacEncoder encoder;
    AudioConfig config{48000, 2, 128, false, 4};
    assert(encoder.init(config));

    std::vector<float> pcm(2048);
    for (size_t i = 0; i < 1024; ++i) {
        pcm[i * 2 + 0] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        pcm[i * 2 + 1] = 0.5f * std::cos(2.0f * 3.14159265f * 880.0f * i / 48000.0f);
    }

    uint8_t out_buf[4096] = {0};

    for (int f = 0; f < 5; ++f) {
        int bytes = encoder.encode_frame(pcm.data(), pcm.size(), out_buf, sizeof(out_buf));
        assert(bytes > 7);

        core::BitReader reader;
        reader.init(out_buf, bytes);
        AdtsHeader header;
        assert(parse_adts_header(reader, header));

        assert(header.syncword == 0xFFF);
        assert(header.sample_rate == 48000);
        assert(header.channel_configuration == 2);
        assert(header.frame_length == bytes);
        assert(header.profile == 1);
    }
}

void test_encode_silent_frames() {
    std::cout << "Testing silent frame encoding...\n";
    AacEncoder encoder;
    AudioConfig config{44100, 2, 128, false, 4};
    assert(encoder.init(config));

    std::vector<float> silence(2048, 0.0f);
    uint8_t out_buf[1024] = {0};

    for (int f = 0; f < 3; ++f) {
        int bytes = encoder.encode_frame(silence.data(), silence.size(), out_buf, sizeof(out_buf));
        assert(bytes > 7);

        core::BitReader reader;
        reader.init(out_buf, bytes);
        AdtsHeader header;
        assert(parse_adts_header(reader, header));
        assert(header.frame_length == bytes);
    }
}

} // anonymous namespace

int main() {
    std::cout << "Starting AAC Encoder unit tests...\n";
    test_encoder_lifecycle_and_validation();
    test_encode_mono_adts_frames();
    test_encode_stereo_adts_frames();
    test_encode_silent_frames();
    std::cout << "All AAC Encoder unit tests passed successfully!\n";
    return 0;
}
