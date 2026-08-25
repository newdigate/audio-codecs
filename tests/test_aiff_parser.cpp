// tests/test_aiff_parser.cpp
#include "src/aiff/decoder/aiff_parser.h"
#include "src/aiff/ieee80.h"
#include "src/aiff/aiff_common.h"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    // Construct standard AIFF header (44100 Hz, stereo, 16-bit, 1000 frames)
    std::vector<uint8_t> header;
    // FORM header (12 bytes)
    header.insert(header.end(), {'F', 'O', 'R', 'M'});
    uint32_t form_size = 4 + (8 + 18) + (8 + 8 + 4000);
    uint8_t sz[4];
    write_be32(sz, form_size);
    header.insert(header.end(), sz, sz + 4);
    header.insert(header.end(), {'A', 'I', 'F', 'F'});

    // Auxiliary chunk with odd size to test padding (ANNO: 7 bytes data + 1 pad byte)
    header.insert(header.end(), {'A', 'N', 'N', 'O'});
    write_be32(sz, 7);
    header.insert(header.end(), sz, sz + 4);
    header.insert(header.end(), {'t', 'e', 's', 't', 'i', 'n', 'g'});
    header.push_back(0); // 1 pad byte

    // COMM chunk (8 + 18 bytes)
    header.insert(header.end(), {'C', 'O', 'M', 'M'});
    write_be32(sz, 18);
    header.insert(header.end(), sz, sz + 4);
    uint8_t comm_data[18];
    write_be16(comm_data, 2); // 2 channels
    write_be32(comm_data + 2, 1000); // 1000 frames
    write_be16(comm_data + 6, 16); // 16 bits
    uint32_to_ieee80(44100, comm_data + 8); // 44100 Hz
    header.insert(header.end(), comm_data, comm_data + 18);

    // SSND chunk header (8 + 8 bytes)
    header.insert(header.end(), {'S', 'S', 'N', 'D'});
    write_be32(sz, 4008);
    header.insert(header.end(), sz, sz + 4);
    uint8_t ssnd_hdr[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // offset=0, blockSize=0
    header.insert(header.end(), ssnd_hdr, ssnd_hdr + 8);

    // Feed in 1-byte chunks
    AiffParser parser;
    size_t offset = 0;
    while (offset < header.size() && !parser.is_header_complete()) {
        size_t consumed = 0;
        bool ok = parser.process_bytes(header.data() + offset, 1, consumed);
        assert(ok);
        offset += consumed;
    }

    assert(parser.is_header_complete());
    assert(parser.get_form_type() == AiffFormType::Aiff);
    assert(parser.get_sample_rate() == 44100);
    assert(parser.get_channels() == 2);
    assert(parser.get_bits_per_sample() == 16);
    assert(parser.get_total_frames() == 1000);
    assert(parser.get_sample_format() == AiffSampleFormat::Int16BE);
    assert(parser.get_data_chunk_size() == 4000);

    // Test AIFC with sowt
    std::vector<uint8_t> aifc_hdr;
    aifc_hdr.insert(aifc_hdr.end(), {'F', 'O', 'R', 'M'});
    write_be32(sz, 1000);
    aifc_hdr.insert(aifc_hdr.end(), sz, sz + 4);
    aifc_hdr.insert(aifc_hdr.end(), {'A', 'I', 'F', 'C'});

    // FVER chunk
    aifc_hdr.insert(aifc_hdr.end(), {'F', 'V', 'E', 'R'});
    write_be32(sz, 4);
    aifc_hdr.insert(aifc_hdr.end(), sz, sz + 4);
    write_be32(sz, kAifcVersion1);
    aifc_hdr.insert(aifc_hdr.end(), sz, sz + 4);

    // COMM chunk with sowt + Pascal string ("sowt" + "\x0e" "Little-endian\0")
    aifc_hdr.insert(aifc_hdr.end(), {'C', 'O', 'M', 'M'});
    write_be32(sz, 38);
    aifc_hdr.insert(aifc_hdr.end(), sz, sz + 4);
    uint8_t comm_c[38];
    write_be16(comm_c, 1); // 1 channel
    write_be32(comm_c + 2, 500); // 500 frames
    write_be16(comm_c + 6, 24); // 24 bits
    uint32_to_ieee80(48000, comm_c + 8);
    write_be32(comm_c + 18, static_cast<uint32_t>(AiffCompressionType::Sowt));
    comm_c[22] = 14; // length 14
    std::memcpy(comm_c + 23, "Little-endian ", 14);
    comm_c[37] = 0; // pad byte
    aifc_hdr.insert(aifc_hdr.end(), comm_c, comm_c + 38);

    // SSND chunk header with non-zero offset (4 bytes offset)
    aifc_hdr.insert(aifc_hdr.end(), {'S', 'S', 'N', 'D'});
    write_be32(sz, 1512); // 1500 audio bytes + 8 header + 4 offset
    aifc_hdr.insert(aifc_hdr.end(), sz, sz + 4);
    uint8_t ssnd_c_hdr[8];
    write_be32(ssnd_c_hdr, 4); // offset = 4
    write_be32(ssnd_c_hdr + 4, 0); // blockSize = 0
    aifc_hdr.insert(aifc_hdr.end(), ssnd_c_hdr, ssnd_c_hdr + 8);
    // 4 bytes of offset padding
    aifc_hdr.insert(aifc_hdr.end(), {0, 0, 0, 0});

    AiffParser aifc_parser;
    offset = 0;
    while (offset < aifc_hdr.size() && !aifc_parser.is_header_complete()) {
        size_t consumed = 0;
        bool ok = aifc_parser.process_bytes(aifc_hdr.data() + offset, aifc_hdr.size() - offset, consumed);
        assert(ok);
        offset += consumed;
    }

    assert(aifc_parser.is_header_complete());
    assert(aifc_parser.get_form_type() == AiffFormType::Aifc);
    assert(aifc_parser.get_compression_type() == AiffCompressionType::Sowt);
    assert(aifc_parser.get_sample_format() == AiffSampleFormat::Int24LE);
    assert(aifc_parser.get_sample_rate() == 48000);
    assert(aifc_parser.get_channels() == 1);
    assert(aifc_parser.get_bits_per_sample() == 24);
    assert(aifc_parser.get_total_frames() == 500);

    std::cout << "AIFF parser tests passed!\n";
    return 0;
}
