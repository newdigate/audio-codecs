#include "audio_codecs/ogg/ogg_demuxer.h"
#include "audio_codecs/ogg/ogg_muxer.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::ogg;

    // 1. Single packet mux -> demux roundtrip
    {
        OggMuxer muxer(0x12345678);
        uint8_t sample_packet[300];
        for (size_t i = 0; i < sizeof(sample_packet); ++i) {
            sample_packet[i] = static_cast<uint8_t>(i & 0xFF);
        }

        assert(muxer.write_packet(sample_packet, sizeof(sample_packet), true, false, 1024));
        uint8_t page_buf[4096];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));
        assert(consumed == static_cast<size_t>(page_len));

        uint8_t pkt_out[1024];
        int64_t gran = 0;
        bool bos = false, eos = false;
        int pkt_len = demuxer.read_packet(pkt_out, sizeof(pkt_out), gran, bos, eos);
        assert(pkt_len == sizeof(sample_packet));
        assert(bos == true);
        assert(eos == false);
        assert(gran == 1024);
        assert(std::memcmp(sample_packet, pkt_out, sizeof(sample_packet)) == 0);
    }

    // 2. Multi-packet in single page
    {
        OggMuxer muxer(0xAABBCCDD);
        uint8_t pkt1[] = {1, 2, 3, 4};
        uint8_t pkt2[] = {5, 6, 7, 8, 9, 10};

        assert(muxer.write_packet(pkt1, sizeof(pkt1), false, false, 2048));
        assert(muxer.write_packet(pkt2, sizeof(pkt2), false, true, 4096));

        uint8_t page_buf[4096];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));

        uint8_t out[128];
        int64_t gran = 0;
        bool bos = false, eos = false;

        int p1_len = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(p1_len == sizeof(pkt1));
        assert(std::memcmp(out, pkt1, sizeof(pkt1)) == 0);

        int p2_len = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(p2_len == sizeof(pkt2));
        assert(eos == true);
        assert(std::memcmp(out, pkt2, sizeof(pkt2)) == 0);
    }

    std::cout << "Ogg framing tests passed!\n";
    return 0;
}
