// tests/test_wav_parser.cpp
#include "src/wav/decoder/wav_parser.h"
#include "src/wav/wav_common.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::wav;

    // Create standard 44-byte WAV header (16-bit stereo 44.1kHz, 1000 data bytes)
    uint8_t header[44] = {
        'R', 'I', 'F', 'F',
        0x0C, 0x04, 0, 0,   // File size - 8 = 1036
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,        // fmt chunk size = 16
        1, 0,               // PCM = 1
        2, 0,               // 2 channels
        0x44, 0xAC, 0, 0,   // 44100 Hz
        0x10, 0xB1, 0x02, 0,// 44100 * 4 = 176400 Bps
        4, 0,               // Block align = 4
        16, 0,              // 16 bits per sample
        'd', 'a', 't', 'a',
        0xE8, 0x03, 0, 0    // 1000 data bytes
    };

    // 1. Test parsing complete header in one call
    WavParser parser;
    size_t consumed = 0;
    bool ok = parser.parse_chunk_stream(header, sizeof(header), consumed);
    assert(ok);
    assert(parser.is_header_complete());
    assert(consumed == 44);
    assert(parser.sample_rate() == 44100);
    assert(parser.channels() == 2);
    assert(parser.bits_per_sample() == 16);
    assert(parser.format_tag() == WavFormat::Pcm);
    assert(parser.sample_format() == WavSampleFormat::Int16LE);
    assert(parser.data_chunk_size() == 1000);
    assert(parser.total_samples() == 250); // 1000 bytes / 4 bytes per frame = 250 frames

    // 2. Test fragmented byte-by-byte streaming parse
    parser.reset();
    for (size_t i = 0; i < sizeof(header); ++i) {
        size_t c = 0;
        ok = parser.parse_chunk_stream(&header[i], 1, c);
        assert(ok);
        assert(c == 1);
    }
    assert(parser.is_header_complete());
    assert(parser.sample_rate() == 44100);
    assert(parser.channels() == 2);

    // 3. Test non-standard chunk ordering (JUNK chunk before fmt, LIST chunk before data)
    std::vector<uint8_t> complex_header = {
        'R', 'I', 'F', 'F',
        0, 0, 0, 0,
        'W', 'A', 'V', 'E',
        // JUNK chunk of 5 bytes (odd size + 1 pad byte = 6 bytes)
        'J', 'U', 'N', 'K',
        5, 0, 0, 0,
        'h', 'e', 'l', 'l', 'o', 0,
        // fmt chunk (16 bytes)
        'f', 'm', 't', ' ',
        16, 0, 0, 0,
        3, 0,               // IEEE Float = 3
        1, 0,               // 1 channel
        0x80, 0xBB, 0, 0,   // 48000 Hz
        0x00, 0xEE, 0x02, 0,// 48000 * 4 = 192000 Bps
        4, 0,               // Block align = 4
        32, 0,              // 32 bits per sample
        // fact chunk
        'f', 'a', 'c', 't',
        4, 0, 0, 0,
        100, 0, 0, 0,       // 100 samples
        // data chunk
        'd', 'a', 't', 'a',
        0x90, 0x01, 0, 0    // 400 bytes
    };

    parser.reset();
    consumed = 0;
    ok = parser.parse_chunk_stream(complex_header.data(), complex_header.size(), consumed);
    assert(ok);
    assert(parser.is_header_complete());
    assert(consumed == complex_header.size());
    assert(parser.format_tag() == WavFormat::IeeeFloat);
    assert(parser.sample_format() == WavSampleFormat::Float32LE);
    assert(parser.channels() == 1);
    assert(parser.sample_rate() == 48000);
    assert(parser.total_samples() == 100);

    std::cout << "WAV parser test passed!\n";
    return 0;
}
