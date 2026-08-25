#include "audio_codecs/vorbis/vorbis_decoder.h"
#include "src/vorbis/encoder/setup_builder.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;

    VorbisDecoder decoder;

    VorbisInfo info;
    info.channels = 2;
    info.sample_rate = 44100;
    info.bitrate_nominal = 128000;
    info.blocksize_0 = 512;
    info.blocksize_1 = 2048;

    uint8_t id_buf[64];
    size_t id_len = build_vorbis_id_header(id_buf, sizeof(id_buf), info);
    assert(decoder.parse_header_packet(id_buf, id_len));

    uint8_t comment_buf[128];
    size_t comment_len = build_vorbis_comment_header(comment_buf, sizeof(comment_buf), "audio_codecs test");
    assert(decoder.parse_header_packet(comment_buf, comment_len));

    uint8_t setup_buf[4096];
    VorbisSetup built_setup;
    size_t setup_len = build_vorbis_setup_header(setup_buf, sizeof(setup_buf), info, built_setup);
    assert(decoder.parse_header_packet(setup_buf, setup_len));
    assert(decoder.has_headers());

    // Construct a silent/empty audio packet (Mode 0: short block)
    // Packet bit 0 = 0 (audio), mode = 0 (short block)
    uint8_t audio_packet[16] = {0}; // mode 0, nonzero=0 (silent floor)
    std::vector<float> pcm_out(2048);

    // First frame initializes overlap buffer
    int samples1 = decoder.decode_packet(audio_packet, sizeof(audio_packet), pcm_out.data(), pcm_out.size());
    assert(samples1 >= 0);

    // Second frame produces decoded overlap-add PCM samples
    int samples2 = decoder.decode_packet(audio_packet, sizeof(audio_packet), pcm_out.data(), pcm_out.size());
    assert(samples2 >= 0);

    std::cout << "VorbisDecoder unit test passed!\n";
    return 0;
}
