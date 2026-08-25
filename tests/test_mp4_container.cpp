#include "include/audio_codecs/mp4/mp4_types.h"
#include "include/audio_codecs/mp4/mp4_demuxer.h"
#include "include/audio_codecs/mp4/mp4_muxer.h"
#include "include/audio_codecs/aac/aac_encoder.h"
#include "include/audio_codecs/aac/aac_decoder.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

using namespace audio_codecs;
using namespace audio_codecs::mp4;
using namespace audio_codecs::aac;

double calculate_snr(const float* ref, const float* test, size_t count) {
    double signal_energy = 0.0;
    double noise_energy = 0.0;

    for (size_t i = 0; i < count; ++i) {
        double s = ref[i];
        double err = ref[i] - test[i];
        signal_energy += s * s;
        noise_energy += err * err;
    }

    if (noise_energy < 1e-15) {
        return 100.0;
    }
    return 10.0 * std::log10(signal_energy / noise_energy);
}

void test_fourcc_constants() {
    std::cout << "Testing FourCC constants...\n";
    assert(FTYP == make_fourcc('f', 't', 'y', 'p'));
    assert(MOOV == make_fourcc('m', 'o', 'o', 'v'));
    assert(MVHD == make_fourcc('m', 'v', 'h', 'd'));
    assert(TRAK == make_fourcc('t', 'r', 'a', 'k'));
    assert(TKHD == make_fourcc('t', 'k', 'h', 'd'));
    assert(MDIA == make_fourcc('m', 'd', 'i', 'a'));
    assert(MDHD == make_fourcc('m', 'd', 'h', 'd'));
    assert(HDLR == make_fourcc('h', 'd', 'l', 'r'));
    assert(MINF == make_fourcc('m', 'i', 'n', 'f'));
    assert(SMHD == make_fourcc('s', 'm', 'h', 'd'));
    assert(DINF == make_fourcc('d', 'i', 'n', 'f'));
    assert(DREF == make_fourcc('d', 'r', 'e', 'f'));
    assert(STBL == make_fourcc('s', 't', 'b', 'l'));
    assert(STSD == make_fourcc('s', 't', 's', 'd'));
    assert(MP4A == make_fourcc('m', 'p', '4', 'a'));
    assert(ESDS == make_fourcc('e', 's', 'd', 's'));
    assert(STTS == make_fourcc('s', 't', 't', 's'));
    assert(STSZ == make_fourcc('s', 't', 's', 'z'));
    assert(STSC == make_fourcc('s', 't', 's', 'c'));
    assert(STCO == make_fourcc('s', 't', 'c', 'o'));
    assert(CO64 == make_fourcc('c', 'o', '6', '4'));
    assert(MDAT == make_fourcc('m', 'd', 'a', 't'));
    assert(FREE == make_fourcc('f', 'r', 'e', 'e'));
}

void test_asc_serialization() {
    std::cout << "Testing AudioSpecificConfig serialization & parsing...\n";

    // 1. Standard 44.1 kHz Stereo AAC-LC
    {
        AudioSpecificConfig asc;
        asc.audio_object_type = 2; // AAC-LC
        asc.sampling_frequency_index = 4; // 44100
        asc.sample_rate = 44100;
        asc.channel_configuration = 2; // Stereo

        std::vector<uint8_t> data = serialize_asc(asc);
        assert(data.size() == 2);
        // Bit layout: 00010 (2) | 0100 (4) | 0010 (2) | 0 0 0 => 0x12, 0x10
        assert(data[0] == 0x12);
        assert(data[1] == 0x10);

        AudioSpecificConfig parsed_asc;
        assert(parse_asc(data.data(), data.size(), parsed_asc));
        assert(parsed_asc.audio_object_type == 2);
        assert(parsed_asc.sampling_frequency_index == 4);
        assert(parsed_asc.sample_rate == 44100);
        assert(parsed_asc.channel_configuration == 2);
    }

    // 2. 48 kHz Mono AAC-LC
    {
        AudioSpecificConfig asc;
        asc.audio_object_type = 2;
        asc.sampling_frequency_index = 3; // 48000
        asc.sample_rate = 48000;
        asc.channel_configuration = 1; // Mono

        std::vector<uint8_t> data = serialize_asc(asc);
        assert(data.size() == 2);
        // Bit layout: 00010 (2) | 0011 (3) | 0001 (1) | 0 0 0 => 0x11, 0x88
        assert(data[0] == 0x11);
        assert(data[1] == 0x88);

        AudioSpecificConfig parsed_asc;
        assert(parse_asc(data.data(), data.size(), parsed_asc));
        assert(parsed_asc.audio_object_type == 2);
        assert(parsed_asc.sampling_frequency_index == 3);
        assert(parsed_asc.sample_rate == 48000);
        assert(parsed_asc.channel_configuration == 1);
    }

    // 3. 22.05 kHz 6-channel (5.1)
    {
        AudioSpecificConfig asc;
        asc.audio_object_type = 2;
        asc.sampling_frequency_index = 7; // 22050
        asc.sample_rate = 22050;
        asc.channel_configuration = 6;

        std::vector<uint8_t> data = serialize_asc(asc);
        assert(data.size() == 2);

        AudioSpecificConfig parsed_asc;
        assert(parse_asc(data.data(), data.size(), parsed_asc));
        assert(parsed_asc.audio_object_type == 2);
        assert(parsed_asc.sampling_frequency_index == 7);
        assert(parsed_asc.sample_rate == 22050);
        assert(parsed_asc.channel_configuration == 6);
    }

    // 4. Custom sample rate with explicit 24-bit frequency (sf_index = 15)
    {
        AudioSpecificConfig asc;
        asc.audio_object_type = 2;
        asc.sampling_frequency_index = 15;
        asc.sample_rate = 50000; // non-standard rate
        asc.channel_configuration = 2;

        std::vector<uint8_t> data = serialize_asc(asc);
        assert(data.size() >= 5);

        AudioSpecificConfig parsed_asc;
        assert(parse_asc(data.data(), data.size(), parsed_asc));
        assert(parsed_asc.audio_object_type == 2);
        assert(parsed_asc.sampling_frequency_index == 15);
        assert(parsed_asc.sample_rate == 50000);
        assert(parsed_asc.channel_configuration == 2);
    }

    // 5. Malformed data handling
    {
        AudioSpecificConfig parsed_asc;
        assert(!parse_asc(nullptr, 0, parsed_asc));
        uint8_t single_byte = 0x12;
        assert(!parse_asc(&single_byte, 1, parsed_asc));
    }
}

void test_muxer_demuxer_synthetic_payloads() {
    std::cout << "Testing Mp4Muxer & Mp4Demuxer with synthetic frames...\n";

    AudioConfig config{44100, 2, 128, false, 4};
    Mp4Muxer muxer;
    assert(muxer.init(config));

    constexpr size_t NUM_SAMPLES = 25;
    std::vector<std::vector<uint8_t>> expected_samples(NUM_SAMPLES);

    for (size_t i = 0; i < NUM_SAMPLES; ++i) {
        size_t sample_len = 50 + (i * 37) % 300; // variable sizes
        expected_samples[i].resize(sample_len);
        for (size_t b = 0; b < sample_len; ++b) {
            expected_samples[i][b] = static_cast<uint8_t>((i * 13 + b * 7 + 0x5A) & 0xFF);
        }
        assert(muxer.add_sample(expected_samples[i].data(), expected_samples[i].size()));
    }

    std::vector<uint8_t> mp4_file = muxer.finalize();
    assert(!mp4_file.empty());
    assert(mp4_file.size() > 100);

    // Verify ISOBMFF header start
    // First 4 bytes: ftyp box size
    // Bytes 4..8: 'ftyp'
    assert(mp4_file[4] == 'f' && mp4_file[5] == 't' && mp4_file[6] == 'y' && mp4_file[7] == 'p');

    Mp4Demuxer demuxer;
    assert(demuxer.open(mp4_file.data(), mp4_file.size()));

    assert(demuxer.get_sample_count() == NUM_SAMPLES);

    AudioConfig recovered_config;
    assert(demuxer.get_audio_config(recovered_config));
    assert(recovered_config.sample_rate == 44100);
    assert(recovered_config.channels == 2);

    AudioSpecificConfig recovered_asc;
    assert(demuxer.get_asc(recovered_asc));
    assert(recovered_asc.sample_rate == 44100);
    assert(recovered_asc.channel_configuration == 2);
    assert(recovered_asc.audio_object_type == 2);

    // Random-access sample verification
    for (size_t i = 0; i < NUM_SAMPLES; ++i) {
        const uint8_t* sample_ptr = nullptr;
        size_t sample_size = 0;
        assert(demuxer.read_sample(i, sample_ptr, sample_size));
        assert(sample_size == expected_samples[i].size());
        assert(std::memcmp(sample_ptr, expected_samples[i].data(), sample_size) == 0);
    }

    // Out-of-bounds sample access check
    {
        const uint8_t* ptr = nullptr;
        size_t size = 0;
        assert(!demuxer.read_sample(NUM_SAMPLES, ptr, size));
    }

    // Sequential streaming read verification
    demuxer.reset();
    for (size_t i = 0; i < NUM_SAMPLES; ++i) {
        const uint8_t* sample_ptr = nullptr;
        size_t sample_size = 0;
        assert(demuxer.read_next_sample(sample_ptr, sample_size));
        assert(sample_size == expected_samples[i].size());
        assert(std::memcmp(sample_ptr, expected_samples[i].data(), sample_size) == 0);
    }

    // EOF on read_next_sample
    {
        const uint8_t* sample_ptr = nullptr;
        size_t sample_size = 0;
        assert(!demuxer.read_next_sample(sample_ptr, sample_size));
    }
}

void test_empty_container() {
    std::cout << "Testing empty container (0 samples)...\n";
    AudioConfig config{48000, 1, 64, false, 4};
    Mp4Muxer muxer;
    assert(muxer.init(config));
    std::vector<uint8_t> mp4_file = muxer.finalize();
    assert(!mp4_file.empty());

    Mp4Demuxer demuxer;
    assert(demuxer.open(mp4_file.data(), mp4_file.size()));
    assert(demuxer.get_sample_count() == 0);

    AudioConfig recovered_config;
    assert(demuxer.get_audio_config(recovered_config));
    assert(recovered_config.sample_rate == 48000);
    assert(recovered_config.channels == 1);
}

void test_end_to_end_aac_m4a_pipeline() {
    std::cout << "Testing End-to-End Pipeline: AacEncoder -> Mp4Muxer -> Mp4Demuxer -> AacDecoder...\n";

    AudioConfig config{44100, 2, 128, false, 4};
    AacEncoder encoder;
    AacDecoder decoder;

    assert(encoder.init(config));

    constexpr size_t NUM_FRAMES = 12;
    constexpr size_t SAMPLES_PER_FRAME = 1024;
    constexpr size_t TOTAL_SAMPLES = NUM_FRAMES * SAMPLES_PER_FRAME * 2;

    std::vector<float> in_pcm(TOTAL_SAMPLES);
    for (size_t i = 0; i < NUM_FRAMES * SAMPLES_PER_FRAME; ++i) {
        in_pcm[i * 2 + 0] = 0.6f * std::sin(2.0 * constants::PI * 440.0 * i / 44100.0);
        in_pcm[i * 2 + 1] = 0.6f * std::cos(2.0 * constants::PI * 880.0 * i / 44100.0);
    }

    Mp4Muxer muxer;
    assert(muxer.init(config));

    uint8_t adts_packet[4096];
    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const float* frame_ptr = &in_pcm[f * SAMPLES_PER_FRAME * 2];
        int bytes = encoder.encode_frame(frame_ptr, SAMPLES_PER_FRAME * 2, adts_packet, sizeof(adts_packet));
        assert(bytes > 7);

        // Strip ADTS header (7 bytes) to extract raw AAC frame payload for M4A container
        const uint8_t* raw_aac = adts_packet + 7;
        size_t raw_size = static_cast<size_t>(bytes - 7);
        assert(muxer.add_sample(raw_aac, raw_size));
    }

    std::vector<uint8_t> m4a_container = muxer.finalize();
    assert(!m4a_container.empty());

    Mp4Demuxer demuxer;
    assert(demuxer.open(m4a_container.data(), m4a_container.size()));
    assert(demuxer.get_sample_count() == NUM_FRAMES);

    AudioConfig demux_config;
    assert(demuxer.get_audio_config(demux_config));
    assert(demux_config.sample_rate == 44100);
    assert(demux_config.channels == 2);

    assert(decoder.init(demux_config));

    std::vector<float> decoded_pcm;
    float out_frame[2048];

    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const uint8_t* raw_ptr = nullptr;
        size_t raw_size = 0;
        assert(demuxer.read_next_sample(raw_ptr, raw_size));
        assert(raw_ptr != nullptr && raw_size > 0);

        int dec_samples = decoder.decode_frame(raw_ptr, raw_size, out_frame, 2048);
        assert(dec_samples == 2048);
        decoded_pcm.insert(decoded_pcm.end(), out_frame, out_frame + dec_samples);
    }

    // Evaluate SNR on steady-state frames (skip encoder/decoder delay)
    size_t eval_start = 2 * SAMPLES_PER_FRAME * 2;
    size_t eval_count = 8 * SAMPLES_PER_FRAME * 2;

    double snr = calculate_snr(&in_pcm[eval_start], &decoded_pcm[eval_start + SAMPLES_PER_FRAME * 2], eval_count);
    std::cout << "End-to-End M4A Pipeline SNR: " << snr << " dB\n";
    assert(snr > 35.0);
}

void test_corrupted_demuxer_inputs() {
    std::cout << "Testing Demuxer resilience to corrupted input data...\n";
    Mp4Demuxer demuxer;
    assert(!demuxer.open(nullptr, 0));
    assert(!demuxer.open(nullptr, 100));

    uint8_t garbage[50] = {1, 2, 3, 4, 5};
    assert(!demuxer.open(garbage, sizeof(garbage)));

    // Box with huge size exceeding buffer
    uint8_t fake_box[16] = {0x7F, 0xFF, 0xFF, 0xFF, 'f', 't', 'y', 'p', 0, 0, 0, 0, 0, 0, 0, 0};
    assert(!demuxer.open(fake_box, sizeof(fake_box)));
}

} // anonymous namespace

int main() {
    std::cout << "Running MP4 / M4A Container Tests...\n";
    test_fourcc_constants();
    test_asc_serialization();
    test_muxer_demuxer_synthetic_payloads();
    test_empty_container();
    test_end_to_end_aac_m4a_pipeline();
    test_corrupted_demuxer_inputs();
    std::cout << "All MP4 Container Tests Passed Successfully!\n";
    return 0;
}
