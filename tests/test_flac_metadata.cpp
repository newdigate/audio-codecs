// tests/test_flac_metadata.cpp
#include "src/flac/metadata.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    FlacStreamInfo orig;
    orig.min_block_size = 4096;
    orig.max_block_size = 4096;
    orig.min_frame_size = 0;
    orig.max_frame_size = 0;
    orig.sample_rate = 44100;
    orig.channels = 2;
    orig.bits_per_sample = 16;
    orig.total_samples = 441000;
    for (int i = 0; i < 16; ++i) {
        orig.md5_signature[i] = static_cast<uint8_t>(i + 1);
    }

    uint8_t buffer[128] = {0};
    size_t written = MetadataBuilder::write_stream_header(buffer, sizeof(buffer), orig);
    assert(written == 42); // "fLaC" (4) + Header (4) + STREAMINFO (34)

    // Check magic
    assert(buffer[0] == 'f' && buffer[1] == 'L' && buffer[2] == 'a' && buffer[3] == 'C');

    // Parse stream header
    FlacStreamInfo parsed;
    size_t consumed = 0;
    bool ok = MetadataParser::parse_stream_header(buffer, written, parsed, consumed);
    assert(ok);
    assert(consumed == 42);
    assert(parsed.min_block_size == 4096);
    assert(parsed.max_block_size == 4096);
    assert(parsed.sample_rate == 44100);
    assert(parsed.channels == 2);
    assert(parsed.bits_per_sample == 16);
    assert(parsed.total_samples == 441000);
    for (int i = 0; i < 16; ++i) {
        assert(parsed.md5_signature[i] == static_cast<uint8_t>(i + 1));
    }

    std::cout << "FLAC Metadata tests passed!\n";
    return 0;
}
