#include "src/vorbis/decoder/header_parser.h"
#include "src/vorbis/encoder/setup_builder.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;

    VorbisInfo info;
    info.channels = 2;
    info.sample_rate = 44100;
    info.bitrate_nominal = 128000;
    info.blocksize_0 = 512;
    info.blocksize_1 = 2048;

    // 1. Build and parse ID header
    uint8_t id_buf[64];
    size_t id_len = build_vorbis_id_header(id_buf, sizeof(id_buf), info);
    assert(id_len > 0);
    assert(is_vorbis_header(id_buf, id_len, VORBIS_PACKET_ID));

    VorbisInfo parsed_info;
    assert(parse_vorbis_id_header(id_buf, id_len, parsed_info));
    assert(parsed_info.channels == 2);
    assert(parsed_info.sample_rate == 44100);
    assert(parsed_info.blocksize_0 == 512);
    assert(parsed_info.blocksize_1 == 2048);

    // 2. Build and parse Comment header
    uint8_t comment_buf[128];
    size_t comment_len = build_vorbis_comment_header(comment_buf, sizeof(comment_buf), "audio_codecs test");
    assert(comment_len > 0);
    assert(is_vorbis_header(comment_buf, comment_len, VORBIS_PACKET_COMMENT));

    VorbisComment parsed_comment;
    assert(parse_vorbis_comment_header(comment_buf, comment_len, parsed_comment));
    assert(parsed_comment.vendor == "audio_codecs test");

    // 3. Build and parse Setup header
    uint8_t setup_buf[4096];
    VorbisSetup built_setup;
    size_t setup_len = build_vorbis_setup_header(setup_buf, sizeof(setup_buf), info, built_setup);
    assert(setup_len > 0);
    assert(is_vorbis_header(setup_buf, setup_len, VORBIS_PACKET_SETUP));

    VorbisSetup parsed_setup;
    assert(parse_vorbis_setup_header(setup_buf, setup_len, info.channels, parsed_setup));
    assert(parsed_setup.codebook_count == 4);
    assert(parsed_setup.floor_count == 1);
    assert(parsed_setup.residue_count == 1);
    assert(parsed_setup.mapping_count == 1);
    assert(parsed_setup.mode_count == 2);
    assert(parsed_setup.modes[0].blockflag == 0);
    assert(parsed_setup.modes[1].blockflag == 1);

    std::cout << "Vorbis Header Builder and Parser tests passed!\n";
    return 0;
}
