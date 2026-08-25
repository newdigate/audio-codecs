#include "include/audio_codecs/aac/aac_decoder.h"
#include "include/audio_codecs/aac/adts_header.h"
#include "src/aac/adts_parser.h"
#include "src/aac/aac_tables.h"
#include "src/aac/decoder/huffman_decoder.h"
#include "src/core/bit_writer.h"
#include "src/core/bit_reader.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace audio_codecs;
using namespace audio_codecs::aac;

// Helper to construct a silent stereo ADTS frame (13 bytes total)
std::vector<uint8_t> create_silent_stereo_adts_frame(uint32_t sample_rate = 44100, bool with_crc = false) {
    uint8_t payload[32] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    // ID_CPE (1)
    pw.write_bits(static_cast<uint32_t>(ElementId::CPE), 3);
    pw.write_bits(0, 4); // element_instance_tag = 0
    pw.write_bits(1, 1); // common_window = 1

    // ics_info (shared)
    pw.write_bits(0, 1); // reserved bit
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(0, 6); // max_sfb = 0
    pw.write_bits(0, 1); // predictor_data_present = 0

    // ms_mask_present = 0 (no M/S)
    pw.write_bits(0, 2);

    // Channel 0 individual channel stream
    pw.write_bits(0, 8); // global_gain = 0
    pw.write_bits(0, 1); // pulse_data_present = 0
    pw.write_bits(0, 1); // tns_data_present = 0
    pw.write_bits(0, 1); // gain_control_data_present = 0

    // Channel 1 individual channel stream
    pw.write_bits(0, 8); // global_gain = 0
    pw.write_bits(0, 1); // pulse_data_present = 0
    pw.write_bits(0, 1); // tns_data_present = 0
    pw.write_bits(0, 1); // gain_control_data_present = 0

    // ID_END (7)
    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = with_crc ? 9 : 7;
    size_t total_frame_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_frame_len, 0);
    AdtsHeader hdr;
    hdr.syncword = 0xFFF;
    hdr.id = 0; // MPEG-4
    hdr.layer = 0;
    hdr.protection_absent = !with_crc;
    hdr.profile = 1; // AAC-LC
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 2; // Stereo
    hdr.frame_length = static_cast<uint16_t>(total_frame_len);
    hdr.adts_buffer_fullness = 0x7FF;
    hdr.num_raw_data_blocks = 0;
    hdr.crc = 0;

    core::BitWriter hw;
    hw.init(frame.data(), total_frame_len);
    write_adts_header(hw, hdr);

    std::memcpy(frame.data() + header_len, payload, payload_len);

    if (with_crc) {
        hdr.crc = calculate_adts_crc(frame.data(), total_frame_len);
        hw.init(frame.data(), header_len);
        write_adts_header(hw, hdr);
    }

    return frame;
}

// Helper to construct a silent mono ADTS frame (11 bytes total)
std::vector<uint8_t> create_silent_mono_adts_frame(uint32_t sample_rate = 44100) {
    uint8_t payload[32] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    // ID_SCE (0)
    pw.write_bits(static_cast<uint32_t>(ElementId::SCE), 3);
    pw.write_bits(0, 4); // element_instance_tag = 0

    // Channel 0 individual channel stream
    pw.write_bits(0, 8); // global_gain = 0

    // ics_info
    pw.write_bits(0, 1); // reserved bit
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(0, 6); // max_sfb = 0
    pw.write_bits(0, 1); // predictor_data_present = 0

    pw.write_bits(0, 1); // pulse_data_present = 0
    pw.write_bits(0, 1); // tns_data_present = 0
    pw.write_bits(0, 1); // gain_control_data_present = 0

    // ID_END (7)
    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_frame_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_frame_len, 0);
    AdtsHeader hdr;
    hdr.syncword = 0xFFF;
    hdr.id = 0;
    hdr.layer = 0;
    hdr.protection_absent = true;
    hdr.profile = 1;
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 1; // Mono
    hdr.frame_length = static_cast<uint16_t>(total_frame_len);
    hdr.adts_buffer_fullness = 0x7FF;
    hdr.num_raw_data_blocks = 0;

    core::BitWriter hw;
    hw.init(frame.data(), total_frame_len);
    write_adts_header(hw, hdr);

    std::memcpy(frame.data() + header_len, payload, payload_len);
    return frame;
}

// Helper to construct a mono ADTS frame with 1 active scalefactor band and non-zero spectral lines
std::vector<uint8_t> create_spectral_mono_adts_frame(uint32_t sample_rate = 44100) {
    uint8_t payload[64] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    // ID_SCE (0)
    pw.write_bits(static_cast<uint32_t>(ElementId::SCE), 3);
    pw.write_bits(0, 4); // element_instance_tag = 0

    // Channel 0
    pw.write_bits(100, 8); // global_gain = 100 (scale factor = 1.0)

    // ics_info
    pw.write_bits(0, 1); // reserved bit
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(1, 6); // max_sfb = 1
    pw.write_bits(0, 1); // predictor_data_present = 0

    // section_data: 1 section of length 1, codebook 1 (signed quad)
    pw.write_bits(1, 4); // sect_cb = 1
    pw.write_bits(1, 5); // sect_len = 1

    // scale_factor_data: delta = 0 for sfb 0 (huffman code for index 60 in CB 12)
    uint32_t sf_code = 0; uint8_t sf_len = 0;
    get_huffman_code(12, 60, sf_code, sf_len);
    pw.write_bits(sf_code, sf_len);

    // pulse, tns, gain
    pw.write_bits(0, 1); // pulse = 0
    pw.write_bits(0, 1); // tns = 0
    pw.write_bits(0, 1); // gain = 0

    // spectral_data: band 0 has width 4 (lines 0..3 for 44.1kHz).
    // Write 1 quad using CB 1: quad (1, 0, -1, 0) -> idx = 2*27 + 1*9 + 0*3 + 1 = 64
    uint32_t sp_code = 0; uint8_t sp_len = 0;
    get_huffman_code(1, 64, sp_code, sp_len);
    pw.write_bits(sp_code, sp_len);

    // ID_END (7)
    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_frame_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_frame_len, 0);
    AdtsHeader hdr;
    hdr.syncword = 0xFFF;
    hdr.id = 0;
    hdr.layer = 0;
    hdr.protection_absent = true;
    hdr.profile = 1;
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 1; // Mono
    hdr.frame_length = static_cast<uint16_t>(total_frame_len);
    hdr.adts_buffer_fullness = 0x7FF;
    hdr.num_raw_data_blocks = 0;

    core::BitWriter hw;
    hw.init(frame.data(), total_frame_len);
    write_adts_header(hw, hdr);

    std::memcpy(frame.data() + header_len, payload, payload_len);
    return frame;
}

// Helper to construct an EightShort stereo ADTS frame
std::vector<uint8_t> create_eight_short_stereo_adts_frame(uint32_t sample_rate = 44100) {
    uint8_t payload[64] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    // ID_CPE (1)
    pw.write_bits(static_cast<uint32_t>(ElementId::CPE), 3);
    pw.write_bits(0, 4); // element_instance_tag = 0
    pw.write_bits(1, 1); // common_window = 1

    // ics_info (EightShort)
    pw.write_bits(0, 1); // reserved bit
    pw.write_bits(static_cast<uint32_t>(WindowSequence::EightShort), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(0, 4); // max_sfb = 0 (silent)
    pw.write_bits(0, 7); // scale_factor_grouping = 0 (8 non-grouped windows)

    // ms_mask_present = 0
    pw.write_bits(0, 2);

    // Channel 0
    pw.write_bits(0, 8); // global_gain = 0
    pw.write_bits(0, 1); // pulse_data_present = 0
    pw.write_bits(0, 1); // tns_data_present = 0
    pw.write_bits(0, 1); // gain_control_data_present = 0

    // Channel 1
    pw.write_bits(0, 8); // global_gain = 0
    pw.write_bits(0, 1); // pulse_data_present = 0
    pw.write_bits(0, 1); // tns_data_present = 0
    pw.write_bits(0, 1); // gain_control_data_present = 0

    // ID_END (7)
    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_frame_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_frame_len, 0);
    AdtsHeader hdr;
    hdr.syncword = 0xFFF;
    hdr.id = 0;
    hdr.layer = 0;
    hdr.protection_absent = true;
    hdr.profile = 1;
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 2; // Stereo
    hdr.frame_length = static_cast<uint16_t>(total_frame_len);
    hdr.adts_buffer_fullness = 0x7FF;
    hdr.num_raw_data_blocks = 0;

    core::BitWriter hw;
    hw.init(frame.data(), total_frame_len);
    write_adts_header(hw, hdr);

    std::memcpy(frame.data() + header_len, payload, payload_len);
    return frame;
}

// Helper to construct a raw silent stereo frame (without ADTS header)
std::vector<uint8_t> create_raw_silent_stereo_payload() {
    uint8_t payload[32] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    pw.write_bits(static_cast<uint32_t>(ElementId::CPE), 3);
    pw.write_bits(0, 4); // instance_tag
    pw.write_bits(1, 1); // common_window

    // ics_info
    pw.write_bits(0, 1);
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(0, 6); // max_sfb = 0
    pw.write_bits(0, 1);

    pw.write_bits(0, 2); // ms_mask_present = 0

    // Ch 0
    pw.write_bits(0, 8);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);

    // Ch 1
    pw.write_bits(0, 8);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);

    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    return std::vector<uint8_t>(payload, payload + pw.get_byte_count());
}

// Helper to construct M/S Stereo ADTS frame
std::vector<uint8_t> create_ms_stereo_adts_frame(uint32_t sample_rate = 44100) {
    uint8_t payload[64] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    pw.write_bits(static_cast<uint32_t>(ElementId::CPE), 3);
    pw.write_bits(0, 4);
    pw.write_bits(1, 1); // common_window = 1

    // ics_info
    pw.write_bits(0, 1);
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(1, 6); // max_sfb = 1
    pw.write_bits(0, 1);

    // ms_mask_present = 2 (all bands active for MS)
    pw.write_bits(2, 2);

    // Channel 0 (Mid)
    pw.write_bits(100, 8); // global_gain = 100
    pw.write_bits(1, 4);   // sect_cb = 1
    pw.write_bits(1, 5);   // sect_len = 1
    uint32_t sf_code; uint8_t sf_len;
    get_huffman_code(12, 60, sf_code, sf_len); // delta = 0
    pw.write_bits(sf_code, sf_len);
    pw.write_bits(0, 1); // pulse = 0
    pw.write_bits(0, 1); // tns = 0
    pw.write_bits(0, 1); // gain = 0
    // Mid quad: (1, 0, 0, 0) -> idx = 2*27 + 1*9 + 1*3 + 1 = 67
    uint32_t sp_code; uint8_t sp_len;
    get_huffman_code(1, 67, sp_code, sp_len);
    pw.write_bits(sp_code, sp_len);

    // Channel 1 (Side)
    pw.write_bits(100, 8); // global_gain = 100
    pw.write_bits(1, 4);   // sect_cb = 1
    pw.write_bits(1, 5);   // sect_len = 1
    pw.write_bits(sf_code, sf_len); // delta = 0
    pw.write_bits(0, 1); // pulse = 0
    pw.write_bits(0, 1); // tns = 0
    pw.write_bits(0, 1); // gain = 0
    // Side quad: (1, 0, 0, 0)
    pw.write_bits(sp_code, sp_len);

    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_len, 0);
    AdtsHeader hdr;
    hdr.frame_length = static_cast<uint16_t>(total_len);
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 2;

    core::BitWriter hw;
    hw.init(frame.data(), total_len);
    write_adts_header(hw, hdr);
    std::memcpy(frame.data() + header_len, payload, payload_len);
    return frame;
}

// Helper to construct Intensity Stereo ADTS frame
std::vector<uint8_t> create_intensity_stereo_adts_frame(uint32_t sample_rate = 44100) {
    uint8_t payload[64] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    pw.write_bits(static_cast<uint32_t>(ElementId::CPE), 3);
    pw.write_bits(0, 4);
    pw.write_bits(1, 1);

    // ics_info
    pw.write_bits(0, 1);
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(1, 6); // max_sfb = 1
    pw.write_bits(0, 1);

    // ms_mask_present = 0
    pw.write_bits(0, 2);

    // Channel 0 (Left)
    pw.write_bits(100, 8);
    pw.write_bits(1, 4); // sect_cb = 1
    pw.write_bits(1, 5); // sect_len = 1
    uint32_t sf_code; uint8_t sf_len;
    get_huffman_code(12, 60, sf_code, sf_len);
    pw.write_bits(sf_code, sf_len);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);
    uint32_t sp_code; uint8_t sp_len;
    get_huffman_code(1, 67, sp_code, sp_len);
    pw.write_bits(sp_code, sp_len);

    // Channel 1 (Right): Intensity stereo (sect_cb = 15, HCB_INTENSITY)
    pw.write_bits(100, 8);
    pw.write_bits(15, 4); // sect_cb = 15 (intensity)
    pw.write_bits(1, 5);  // sect_len = 1
    pw.write_bits(sf_code, sf_len); // is_pos delta = 0 -> is_pos = 0 -> scale = 1.0
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);
    pw.write_bits(0, 1);
    // No spectral bits needed for intensity band

    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_len, 0);
    AdtsHeader hdr;
    hdr.frame_length = static_cast<uint16_t>(total_len);
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 2;

    core::BitWriter hw;
    hw.init(frame.data(), total_len);
    write_adts_header(hw, hdr);
    std::memcpy(frame.data() + header_len, payload, payload_len);
    return frame;
}

// Helper to construct TNS ADTS frame
std::vector<uint8_t> create_tns_adts_frame(uint32_t sample_rate = 44100) {
    uint8_t payload[64] = {0};
    core::BitWriter pw;
    pw.init(payload, sizeof(payload));

    pw.write_bits(static_cast<uint32_t>(ElementId::SCE), 3);
    pw.write_bits(0, 4);

    pw.write_bits(100, 8); // global_gain = 100

    // ics_info
    pw.write_bits(0, 1);
    pw.write_bits(static_cast<uint32_t>(WindowSequence::OnlyLong), 2);
    pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
    pw.write_bits(1, 6); // max_sfb = 1
    pw.write_bits(0, 1);

    // section_data
    pw.write_bits(1, 4); // cb = 1
    pw.write_bits(1, 5); // len = 1

    // scale_factor_data
    uint32_t sf_code; uint8_t sf_len;
    get_huffman_code(12, 60, sf_code, sf_len);
    pw.write_bits(sf_code, sf_len);

    pw.write_bits(0, 1); // pulse = 0

    // TNS data: tns_data_present = 1
    pw.write_bits(1, 1);
    pw.write_bits(1, 2); // n_filt = 1
    pw.write_bits(0, 1); // coef_res = 0 (3 bits)
    pw.write_bits(1, 6); // length = 1
    pw.write_bits(1, 5); // order = 1
    pw.write_bits(0, 1); // direction = upward (0)
    pw.write_bits(0, 1); // coef_compress = 0
    pw.write_bits(2, 3); // raw_coef[0] = 2

    pw.write_bits(0, 1); // gain = 0

    // spectral_data: 1 quad
    uint32_t sp_code; uint8_t sp_len;
    get_huffman_code(1, 67, sp_code, sp_len);
    pw.write_bits(sp_code, sp_len);

    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_len = header_len + payload_len;

    std::vector<uint8_t> frame(total_len, 0);
    AdtsHeader hdr;
    hdr.frame_length = static_cast<uint16_t>(total_len);
    hdr.sampling_frequency_index = static_cast<uint8_t>(get_sampling_frequency_index(sample_rate));
    hdr.sample_rate = sample_rate;
    hdr.channel_configuration = 1;

    core::BitWriter hw;
    hw.init(frame.data(), total_len);
    write_adts_header(hw, hdr);
    std::memcpy(frame.data() + header_len, payload, payload_len);
    return frame;
}

} // anonymous namespace

void test_decoder_lifecycle() {
    std::cout << "Testing AAC decoder lifecycle and error handling...\n";
    AacDecoder decoder;

    AudioConfig config;
    config.sample_rate = 44100;
    config.channels = 2;
    config.bitrate_kbps = 128;

    assert(decoder.init(config));
    decoder.reset();

    // Invalid parameters
    float pcm[2048];
    assert(decoder.decode_frame(nullptr, 100, pcm, 2048) < 0);
    uint8_t dummy[10] = {0};
    assert(decoder.decode_frame(dummy, 10, nullptr, 2048) < 0);
    assert(decoder.decode_frame(dummy, 2, pcm, 2048) < 0); // Too short

    // Insufficient output buffer
    auto silent_stereo = create_silent_stereo_adts_frame(44100);
    assert(decoder.decode_frame(silent_stereo.data(), silent_stereo.size(), pcm, 100) < 0);
}

void test_decode_silent_stereo_adts() {
    std::cout << "Testing silent stereo ADTS decoding...\n";
    AacDecoder decoder;
    AudioConfig config{44100, 2, 128, false, 4};
    assert(decoder.init(config));

    auto frame = create_silent_stereo_adts_frame(44100);
    std::vector<float> pcm(2048, -999.0f);

    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 2048); // 1024 stereo pairs

    // Verify all samples are zero
    for (size_t i = 0; i < 2048; ++i) {
        assert(std::fabs(pcm[i]) < 1e-6f);
    }

    uint32_t sr = 0;
    uint8_t ch = 0;
    uint32_t br = 0;
    assert(decoder.get_frame_info(sr, ch, br));
    assert(sr == 44100);
    assert(ch == 2);
    assert(decoder.get_last_frame_bytes() == frame.size());
    assert(decoder.get_last_sync_offset() == 0);
}

void test_decode_silent_mono_adts() {
    std::cout << "Testing silent mono ADTS decoding...\n";
    AacDecoder decoder;
    AudioConfig config{48000, 1, 64, false, 4};
    assert(decoder.init(config));

    auto frame = create_silent_mono_adts_frame(48000);
    std::vector<float> pcm(1024, -999.0f);

    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 1024); // 1024 mono samples

    for (size_t i = 0; i < 1024; ++i) {
        assert(std::fabs(pcm[i]) < 1e-6f);
    }

    uint32_t sr = 0;
    uint8_t ch = 0;
    uint32_t br = 0;
    assert(decoder.get_frame_info(sr, ch, br));
    assert(sr == 48000);
    assert(ch == 1);
}

void test_decode_adts_with_crc() {
    std::cout << "Testing ADTS frame decoding with 16-bit CRC...\n";
    AacDecoder decoder;
    auto frame = create_silent_stereo_adts_frame(44100, true);
    assert(frame.size() == 9 + 6); // 9 byte header + 6 byte payload

    std::vector<float> pcm(2048, 0.0f);
    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 2048);
}

void test_decode_raw_frame() {
    std::cout << "Testing raw AAC frame decoding without ADTS header...\n";
    AacDecoder decoder;
    AudioConfig config{44100, 2, 128, false, 4};
    assert(decoder.init(config));

    auto raw_payload = create_raw_silent_stereo_payload();
    std::vector<float> pcm(2048, -999.0f);

    int samples = decoder.decode_frame(raw_payload.data(), raw_payload.size(), pcm.data(), pcm.size());
    assert(samples == 2048);
    for (size_t i = 0; i < 2048; ++i) {
        assert(std::fabs(pcm[i]) < 1e-6f);
    }
}

void test_decode_spectral_audio_frame() {
    std::cout << "Testing spectral audio frame decoding & overlap-add...\n";
    AacDecoder decoder;
    auto frame = create_spectral_mono_adts_frame(44100);

    std::vector<float> pcm1(1024, 0.0f);
    int samples1 = decoder.decode_frame(frame.data(), frame.size(), pcm1.data(), pcm1.size());
    assert(samples1 == 1024);

    // First frame overlap output should be non-zero due to non-zero spectral coefficients
    bool has_nonzero = false;
    for (float s : pcm1) {
        assert(!std::isnan(s) && !std::isinf(s));
        assert(std::fabs(s) <= 2.0f);
        if (std::fabs(s) > 1e-5f) {
            has_nonzero = true;
        }
    }
    assert(has_nonzero);

    // Decode a second frame to verify overlap-add continuity
    std::vector<float> pcm2(1024, 0.0f);
    int samples2 = decoder.decode_frame(frame.data(), frame.size(), pcm2.data(), pcm2.size());
    assert(samples2 == 1024);
    for (float s : pcm2) {
        assert(!std::isnan(s) && !std::isinf(s));
    }
}

void test_decode_eight_short_frame() {
    std::cout << "Testing EightShort window sequence decoding...\n";
    AacDecoder decoder;
    auto frame = create_eight_short_stereo_adts_frame(44100);

    std::vector<float> pcm(2048, 0.0f);
    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 2048);
}

void test_decode_ms_stereo_frame() {
    std::cout << "Testing M/S Stereo frame decoding...\n";
    AacDecoder decoder;
    auto frame = create_ms_stereo_adts_frame(44100);

    std::vector<float> pcm(2048, 0.0f);
    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 2048);
    for (float s : pcm) {
        assert(!std::isnan(s) && !std::isinf(s));
    }
}

void test_decode_intensity_stereo_frame() {
    std::cout << "Testing Intensity Stereo frame decoding...\n";
    AacDecoder decoder;
    auto frame = create_intensity_stereo_adts_frame(44100);

    std::vector<float> pcm(2048, 0.0f);
    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 2048);
    for (float s : pcm) {
        assert(!std::isnan(s) && !std::isinf(s));
    }
}

void test_decode_tns_frame() {
    std::cout << "Testing TNS frame decoding...\n";
    AacDecoder decoder;
    auto frame = create_tns_adts_frame(44100);

    std::vector<float> pcm(1024, 0.0f);
    int samples = decoder.decode_frame(frame.data(), frame.size(), pcm.data(), pcm.size());
    assert(samples == 1024);
    for (float s : pcm) {
        assert(!std::isnan(s) && !std::isinf(s));
    }
}

void test_multi_frame_stream_playback() {
    std::cout << "Testing multi-frame continuous bitstream decoding...\n";
    AacDecoder decoder;

    // Concatenate 5 consecutive frames into a single bitstream
    std::vector<uint8_t> stream;
    for (int f = 0; f < 5; ++f) {
        auto frame = create_silent_stereo_adts_frame(44100);
        stream.insert(stream.end(), frame.begin(), frame.end());
    }

    size_t offset = 0;
    int total_frames_decoded = 0;
    std::vector<float> pcm(2048, 0.0f);

    while (offset < stream.size()) {
        int samples = decoder.decode_frame(stream.data() + offset, stream.size() - offset, pcm.data(), pcm.size());
        assert(samples == 2048);
        size_t frame_bytes = decoder.get_last_frame_bytes();
        assert(frame_bytes > 0);
        offset += frame_bytes + decoder.get_last_sync_offset();
        total_frames_decoded++;
    }

    assert(total_frames_decoded == 5);
}

int main() {
    std::cout << "Starting AAC Decoder unit tests...\n";
    test_decoder_lifecycle();
    test_decode_silent_stereo_adts();
    test_decode_silent_mono_adts();
    test_decode_adts_with_crc();
    test_decode_raw_frame();
    test_decode_spectral_audio_frame();
    test_decode_eight_short_frame();
    test_decode_ms_stereo_frame();
    test_decode_intensity_stereo_frame();
    test_decode_tns_frame();
    test_multi_frame_stream_playback();
    std::cout << "All AAC Decoder unit tests passed!\n";
    return 0;
}
