#include "include/audio_codecs/aac/adts_header.h"
#include "src/aac/adts_parser.h"
#include "src/aac/aac_tables.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

void test_adts_header_defaults() {
    using namespace audio_codecs::aac;

    AdtsHeader header;
    assert(header.syncword == 0xFFF);
    assert(header.id == 0);
    assert(header.layer == 0);
    assert(header.protection_absent == true);
    assert(header.profile == 1);
    assert(header.sampling_frequency_index == 4);
    assert(header.sample_rate == 44100);
    assert(header.channel_configuration == 2);
    assert(header.header_size_bytes() == 7);

    header.protection_absent = false;
    assert(header.header_size_bytes() == 9);

    AdtsHeader header2;
    assert(header2 == AdtsHeader{});
    assert(header != header2);
}

void test_adts_7byte_roundtrip() {
    using namespace audio_codecs::aac;

    // Test cases with various parameters
    struct TestCase {
        uint8_t id;
        uint8_t profile;
        uint8_t sf_index;
        uint8_t channels;
        uint16_t frame_len;
        uint16_t buffer_fullness;
        uint8_t num_blocks;
    };

    TestCase cases[] = {
        {0, 1, 4, 2, 500, 0x7FF, 0},   // MPEG-4 AAC-LC 44.1kHz stereo
        {1, 1, 3, 1, 300, 0x500, 0},   // MPEG-2 AAC-LC 48kHz mono
        {0, 0, 0, 6, 1200, 0x123, 1},  // MPEG-4 AAC-Main 96kHz 5.1ch
        {0, 2, 7, 2, 7, 0x000, 0},     // Minimum length header
        {1, 1, 11, 1, 8191, 0x7FF, 3}, // Max 13-bit length, 8kHz
    };

    for (const auto& tc : cases) {
        AdtsHeader hdr_out;
        hdr_out.id = tc.id;
        hdr_out.profile = tc.profile;
        hdr_out.sampling_frequency_index = tc.sf_index;
        hdr_out.channel_configuration = tc.channels;
        hdr_out.frame_length = tc.frame_len;
        hdr_out.adts_buffer_fullness = tc.buffer_fullness;
        hdr_out.num_raw_data_blocks = tc.num_blocks;
        hdr_out.protection_absent = true;

        uint8_t buffer[16] = {0};
        audio_codecs::core::BitWriter writer;
        writer.init(buffer, sizeof(buffer));

        size_t written = write_adts_header(writer, hdr_out);
        assert(written == 7);
        assert(writer.get_byte_count() == 7);

        audio_codecs::core::BitReader reader;
        reader.init(buffer, 7);

        AdtsHeader hdr_in;
        bool ok = parse_adts_header(reader, hdr_in);
        assert(ok);
        assert(hdr_in.syncword == 0xFFF);
        assert(hdr_in.id == tc.id);
        assert(hdr_in.layer == 0);
        assert(hdr_in.protection_absent == true);
        assert(hdr_in.profile == tc.profile);
        assert(hdr_in.sampling_frequency_index == tc.sf_index);
        assert(hdr_in.sample_rate == get_sample_rate_from_index(tc.sf_index));
        assert(hdr_in.channel_configuration == tc.channels);
        assert(hdr_in.frame_length == tc.frame_len);
        assert(hdr_in.adts_buffer_fullness == tc.buffer_fullness);
        assert(hdr_in.num_raw_data_blocks == tc.num_blocks);
        assert(hdr_in.crc == 0);
        assert(hdr_in.header_size_bytes() == 7);
    }
}

void test_all_sampling_frequencies_roundtrip() {
    using namespace audio_codecs::aac;

    for (uint8_t sf_idx = 0; sf_idx < 13; ++sf_idx) {
        AdtsHeader hdr_out;
        hdr_out.sampling_frequency_index = sf_idx;
        hdr_out.frame_length = 100;

        uint8_t buffer[16] = {0};
        audio_codecs::core::BitWriter writer;
        writer.init(buffer, sizeof(buffer));
        write_adts_header(writer, hdr_out);

        audio_codecs::core::BitReader reader;
        reader.init(buffer, sizeof(buffer));
        AdtsHeader hdr_in;
        bool ok = parse_adts_header(reader, hdr_in);
        assert(ok);
        assert(hdr_in.sampling_frequency_index == sf_idx);
        assert(hdr_in.sample_rate == get_sample_rate_from_index(sf_idx));
    }
}

void test_adts_9byte_crc_roundtrip() {
    using namespace audio_codecs::aac;

    // Create a frame with 9-byte header and 32 bytes of mock payload
    const size_t payload_len = 32;
    const size_t total_frame_len = 9 + payload_len;
    std::vector<uint8_t> frame(total_frame_len, 0);

    // Fill mock payload
    for (size_t i = 0; i < payload_len; ++i) {
        frame[9 + i] = static_cast<uint8_t>((i * 37 + 13) & 0xFF);
    }

    AdtsHeader hdr_out;
    hdr_out.id = 0; // MPEG-4
    hdr_out.profile = 1; // AAC-LC
    hdr_out.sampling_frequency_index = 4; // 44.1kHz
    hdr_out.channel_configuration = 2; // Stereo
    hdr_out.protection_absent = false; // CRC present
    hdr_out.frame_length = static_cast<uint16_t>(total_frame_len);
    hdr_out.adts_buffer_fullness = 0x7FF;
    hdr_out.num_raw_data_blocks = 0;
    hdr_out.crc = 0; // Temporary before calculation

    // Write preliminary header
    audio_codecs::core::BitWriter writer;
    writer.init(frame.data(), total_frame_len);
    size_t written = write_adts_header(writer, hdr_out);
    assert(written == 9);

    // Calculate CRC over frame (which skips bytes 7-8)
    uint16_t calculated_crc = calculate_adts_crc(frame.data(), total_frame_len);
    assert(calculated_crc != 0); // CRC should be non-zero for real data
    assert(calculated_crc != 0xFFFF);

    // Update CRC in header and re-serialize header
    hdr_out.crc = calculated_crc;
    writer.init(frame.data(), total_frame_len);
    write_adts_header(writer, hdr_out);

    // Verify CRC was written into bytes 7 and 8 (MSB first)
    assert(frame[7] == static_cast<uint8_t>((calculated_crc >> 8) & 0xFF));
    assert(frame[8] == static_cast<uint8_t>(calculated_crc & 0xFF));

    // Parse the header from the complete frame
    audio_codecs::core::BitReader reader;
    reader.init(frame.data(), total_frame_len);

    AdtsHeader hdr_in;
    bool ok = parse_adts_header(reader, hdr_in);
    assert(ok);
    assert(hdr_in.protection_absent == false);
    assert(hdr_in.header_size_bytes() == 9);
    assert(hdr_in.crc == calculated_crc);
    assert(hdr_in.frame_length == total_frame_len);

    // Verify CRC validity on the intact frame
    uint16_t verify_crc = calculate_adts_crc(frame.data(), total_frame_len);
    assert(verify_crc == hdr_in.crc);

    // Corrupt 1 byte in payload and verify CRC fails
    frame[15] ^= 0x01;
    uint16_t corrupted_crc = calculate_adts_crc(frame.data(), total_frame_len);
    assert(corrupted_crc != hdr_in.crc);

    // Restore payload and corrupt 1 byte in header (byte 2: profile/sample_rate)
    frame[15] ^= 0x01;
    assert(calculate_adts_crc(frame.data(), total_frame_len) == hdr_in.crc);
    frame[2] ^= 0x04;
    corrupted_crc = calculate_adts_crc(frame.data(), total_frame_len);
    assert(corrupted_crc != hdr_in.crc);
}

void test_adts_crc_direct() {
    using namespace audio_codecs::aac;

    // Direct CRC tests on empty / null
    assert(calculate_adts_crc(nullptr, 0) == 0xFFFF);
    assert(calculate_adts_crc(nullptr, 10) == 0xFFFF);

    uint8_t test_data[] = "123456789";
    uint16_t crc = calculate_adts_crc(test_data, 9);
    assert(crc != 0xFFFF);
}

void test_adts_sync_search() {
    using namespace audio_codecs::aac;

    // Create a stream buffer with junk preamble, valid frame 1, junk interlude, valid frame 2
    std::vector<uint8_t> stream;

    // Junk preamble (50 bytes with some deceptive 0xFF bytes)
    for (size_t i = 0; i < 50; ++i) {
        if (i == 10) stream.push_back(0xFF);
        else if (i == 11) stream.push_back(0x00); // 0xFF followed by 0x00 (not sync)
        else if (i == 25) stream.push_back(0xFF);
        else if (i == 26) stream.push_back(0x70); // 0xFF followed by 0x70 (not sync)
        else stream.push_back(static_cast<uint8_t>((i * 59 + 7) & 0xFF));
    }

    size_t expected_frame1_pos = stream.size();

    // Frame 1: 7-byte header + 20 bytes payload
    AdtsHeader hdr1;
    hdr1.frame_length = 27;
    hdr1.protection_absent = true;
    hdr1.sampling_frequency_index = 4;
    hdr1.channel_configuration = 2;

    uint8_t frame1_buf[27] = {0};
    audio_codecs::core::BitWriter w1;
    w1.init(frame1_buf, sizeof(frame1_buf));
    write_adts_header(w1, hdr1);
    for (size_t i = 7; i < 27; ++i) frame1_buf[i] = static_cast<uint8_t>(i);

    stream.insert(stream.end(), frame1_buf, frame1_buf + 27);

    // Junk interlude
    for (size_t i = 0; i < 20; ++i) {
        stream.push_back(static_cast<uint8_t>(i ^ 0x55));
    }

    size_t expected_frame2_pos = stream.size();

    // Frame 2: 9-byte header with CRC + 15 bytes payload
    AdtsHeader hdr2;
    hdr2.frame_length = 24;
    hdr2.protection_absent = false;
    hdr2.sampling_frequency_index = 3; // 48kHz
    hdr2.channel_configuration = 1; // mono

    uint8_t frame2_buf[24] = {0};
    audio_codecs::core::BitWriter w2;
    w2.init(frame2_buf, sizeof(frame2_buf));
    write_adts_header(w2, hdr2);
    for (size_t i = 9; i < 24; ++i) frame2_buf[i] = static_cast<uint8_t>(i * 3);
    hdr2.crc = calculate_adts_crc(frame2_buf, 24);
    w2.init(frame2_buf, sizeof(frame2_buf));
    write_adts_header(w2, hdr2);

    stream.insert(stream.end(), frame2_buf, frame2_buf + 24);

    // Find Frame 1
    size_t offset = 0;
    bool found = find_adts_sync(stream.data(), stream.size(), offset);
    assert(found);
    assert(offset == expected_frame1_pos);

    // Parse Frame 1 at offset
    audio_codecs::core::BitReader r1;
    r1.init(stream.data() + offset, stream.size() - offset);
    AdtsHeader parsed_hdr1;
    assert(parse_adts_header(r1, parsed_hdr1));
    assert(parsed_hdr1.frame_length == 27);
    assert(parsed_hdr1.sampling_frequency_index == 4);

    // Search after Frame 1 to find Frame 2
    size_t next_search_pos = offset + parsed_hdr1.frame_length;
    size_t offset2 = 0;
    found = find_adts_sync(stream.data() + next_search_pos, stream.size() - next_search_pos, offset2);
    assert(found);
    assert(next_search_pos + offset2 == expected_frame2_pos);

    // Parse Frame 2 at offset2
    audio_codecs::core::BitReader r2;
    r2.init(stream.data() + next_search_pos + offset2, stream.size() - (next_search_pos + offset2));
    AdtsHeader parsed_hdr2;
    assert(parse_adts_header(r2, parsed_hdr2));
    assert(parsed_hdr2.frame_length == 24);
    assert(parsed_hdr2.sampling_frequency_index == 3);
    assert(parsed_hdr2.protection_absent == false);
    assert(parsed_hdr2.crc == hdr2.crc);

    // Search in a region with no syncword
    std::vector<uint8_t> no_sync(100, 0x00);
    size_t dummy_off = 0;
    assert(!find_adts_sync(no_sync.data(), no_sync.size(), dummy_off));
    assert(!find_adts_sync(nullptr, 100, dummy_off));
    assert(!find_adts_sync(no_sync.data(), 1, dummy_off));
}

void test_adts_parser_edge_cases() {
    using namespace audio_codecs::aac;

    // 1. Buffer too small (< 7 bytes)
    uint8_t small_buf[6] = {0xFF, 0xF1, 0x50, 0x80, 0x00, 0x1F};
    audio_codecs::core::BitReader r_small;
    r_small.init(small_buf, sizeof(small_buf));
    AdtsHeader hdr;
    assert(!parse_adts_header(r_small, hdr));

    // 2. Bad syncword (0xFFE instead of 0xFFF)
    uint8_t bad_sync[7] = {0xFF, 0xE1, 0x50, 0x80, 0x00, 0x1F, 0xFC};
    audio_codecs::core::BitReader r_bad_sync;
    r_bad_sync.init(bad_sync, sizeof(bad_sync));
    assert(!parse_adts_header(r_bad_sync, hdr));

    // 3. Bad layer (layer != 0)
    uint8_t bad_layer[7] = {0xFF, 0xF3, 0x50, 0x80, 0x00, 0x1F, 0xFC}; // layer = 1 ('01')
    audio_codecs::core::BitReader r_bad_layer;
    r_bad_layer.init(bad_layer, sizeof(bad_layer));
    assert(!parse_adts_header(r_bad_layer, hdr));

    // 4. Frame length too small (< 7 for 7-byte header)
    AdtsHeader bad_len_hdr;
    bad_len_hdr.frame_length = 5;
    bad_len_hdr.protection_absent = true;
    uint8_t bad_len_buf[7] = {0};
    audio_codecs::core::BitWriter w_bad_len;
    w_bad_len.init(bad_len_buf, sizeof(bad_len_buf));
    write_adts_header(w_bad_len, bad_len_hdr);
    audio_codecs::core::BitReader r_bad_len;
    r_bad_len.init(bad_len_buf, sizeof(bad_len_buf));
    assert(!parse_adts_header(r_bad_len, hdr));

    // 5. CRC present but only 7 bytes provided
    AdtsHeader crc_hdr;
    crc_hdr.protection_absent = false;
    crc_hdr.frame_length = 20;
    uint8_t crc_7_buf[7] = {0};
    audio_codecs::core::BitWriter w_crc_7;
    w_crc_7.init(crc_7_buf, sizeof(crc_7_buf));
    write_adts_header(w_crc_7, crc_hdr); // writes 7 bytes because buf is 7 bytes
    audio_codecs::core::BitReader r_crc_7;
    r_crc_7.init(crc_7_buf, sizeof(crc_7_buf)); // only 7 bytes -> should fail because needs 9
    assert(!parse_adts_header(r_crc_7, hdr));
}

int main() {
    std::cout << "Testing ADTS Framing...\n";
    test_adts_header_defaults();
    test_adts_7byte_roundtrip();
    test_all_sampling_frequencies_roundtrip();
    test_adts_9byte_crc_roundtrip();
    test_adts_crc_direct();
    test_adts_sync_search();
    test_adts_parser_edge_cases();
    std::cout << "ADTS Framing tests passed!\n";
    return 0;
}
