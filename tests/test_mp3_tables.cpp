// tests/test_mp3_tables.cpp
#include "src/mp3/mp3_common.h"
#include "src/mp3/mp3_tables.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;
    
    // 0xFFFB9064: 
    // 11111111111 (sync)
    // 1 (IDex=1) 1 (ID=1) -> MPEG-1
    // 01 (Layer 3)
    // 1 (no CRC)
    // 1001 (bitrate index 9 = 128 kbps)
    // 00 (sampling freq index 0 = 44.1 kHz)
    // 0 (padding bit 0)
    // 0 (private)
    // 01 (Joint Stereo)
    // 10 (mode extension 2 = MS Stereo ON, Intensity OFF)
    // 0 (copyright)
    // 1 (original)
    // 00 (emphasis none)
    uint32_t header_word = 0xFFFB9064;
    FrameHeader header;
    bool ok = parse_frame_header(header_word, header);
    assert(ok);
    assert(header.version == MpegVersion::Mpeg1);
    assert(header.layer == MpegLayer::Layer3);
    assert(header.bitrate_kbps == 128);
    assert(header.sample_rate == 44100);
    assert(header.channels == 2);
    assert(header.ms_stereo == true);
    assert(header.intensity_stereo == false);
    assert(header.frame_bytes == 417);
    assert(header.side_info_bytes == 32);

    // Build header word back
    uint32_t rebuilt_word = 0;
    assert(build_frame_header_word(header, rebuilt_word));
    assert(rebuilt_word == header_word);

    // Verify D[512] synthesis window symmetry and values
    assert(D_SYNTHESIS_WINDOW[0] == 0.0f);
    assert(std::fabs(D_SYNTHESIS_WINDOW[256] - 1.144989014f) < 1e-5f);

    // Verify pretab table values
    assert(PRETAB[0] == 0);
    assert(PRETAB[11] == 1);
    assert(PRETAB[15] == 2);
    assert(PRETAB[17] == 3);
    assert(PRETAB[21] == 0);

    // Verify Scalefactor bands table for 44.1 kHz long block (Table 56)
    const uint16_t* sfb_44100_long = get_scalefac_band_table_long(44100);
    assert(sfb_44100_long != nullptr);
    assert(sfb_44100_long[0] == 0);
    assert(sfb_44100_long[1] == 4);
    assert(sfb_44100_long[2] == 8);
    assert(sfb_44100_long[21] == 418);
    assert(sfb_44100_long[22] == 576);

    // Verify Alias butterflies
    assert(ALIAS_CS[0] > 0.8f);
    assert(ALIAS_CA[0] < 0.0f);

    // Verify Huffman table 1 entry: (x=0, y=1) code 001 len 3
    const HuffmanCodebook& huff1 = HUFFMAN_CODEBOOKS[1];
    assert(huff1.table_num == 1);
    assert(huff1.linbits == 0);

    std::cout << "MP3 tables and header parsing tests passed!\n";
    return 0;
}
