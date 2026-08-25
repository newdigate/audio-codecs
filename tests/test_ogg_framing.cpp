#include "include/audio_codecs/ogg/ogg_demuxer.h"
#include "include/audio_codecs/ogg/ogg_muxer.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::ogg;

    // 1. Exact single packet roundtrip from brief
    {
        OggMuxer muxer(0x12345678);
        uint8_t sample_packet[300];
        for (size_t i = 0; i < sizeof(sample_packet); ++i) sample_packet[i] = static_cast<uint8_t>(i & 0xFF);

        assert(muxer.write_packet(sample_packet, sizeof(sample_packet), true, false, 0));
        uint8_t page_buf[4096];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));

        uint8_t pkt_out[1024];
        int64_t gran = 0;
        bool bos = false, eos = false;
        int pkt_len = demuxer.read_packet(pkt_out, sizeof(pkt_out), gran, bos, eos);
        assert(pkt_len == sizeof(sample_packet));
        assert(bos == true);
        assert(eos == false);
        assert(gran == 0);
        assert(std::memcmp(sample_packet, pkt_out, sizeof(sample_packet)) == 0);
    }

    // 2. Multi-packet in single page
    {
        OggMuxer muxer(0xAABBCCDD);
        uint8_t pkt1[] = {1, 2, 3, 4};
        uint8_t pkt2[] = {5, 6, 7, 8, 9, 10};

        assert(muxer.write_packet(pkt1, sizeof(pkt1), true, false, 1000));
        assert(muxer.write_packet(pkt2, sizeof(pkt2), false, true, 2000));

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
        assert(bos == true);
        assert(eos == false);
        assert(std::memcmp(out, pkt1, sizeof(pkt1)) == 0);

        int p2_len = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(p2_len == sizeof(pkt2));
        assert(bos == false);
        assert(eos == true);
        assert(gran == 2000);
        assert(std::memcmp(out, pkt2, sizeof(pkt2)) == 0);
    }

    // 3. Exact 255-byte and 510-byte packet boundary tests (lacing terminator check)
    {
        OggMuxer muxer(0x55667788);
        uint8_t pkt_255[255];
        for (size_t i = 0; i < sizeof(pkt_255); ++i) pkt_255[i] = static_cast<uint8_t>(i);
        uint8_t pkt_510[510];
        for (size_t i = 0; i < sizeof(pkt_510); ++i) pkt_510[i] = static_cast<uint8_t>(i ^ 0xAA);

        assert(muxer.write_packet(pkt_255, sizeof(pkt_255), true, false, 500));
        assert(muxer.write_packet(pkt_510, sizeof(pkt_510), false, true, 1000));

        uint8_t page_buf[4096];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));

        uint8_t out[1024];
        int64_t gran = 0;
        bool bos = false, eos = false;

        int r1 = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(r1 == 255);
        assert(bos == true);
        assert(std::memcmp(out, pkt_255, 255) == 0);

        int r2 = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(r2 == 510);
        assert(eos == true);
        assert(std::memcmp(out, pkt_510, 510) == 0);
    }

    // 4. Large multi-segment packet (> 1500 bytes)
    {
        OggMuxer muxer(0x99887766);
        std::vector<uint8_t> large_pkt(1800);
        for (size_t i = 0; i < large_pkt.size(); ++i) {
            large_pkt[i] = static_cast<uint8_t>((i * 7) & 0xFF);
        }

        assert(muxer.write_packet(large_pkt.data(), large_pkt.size(), true, true, 99999));
        uint8_t page_buf[4096];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));

        std::vector<uint8_t> out(2048);
        int64_t gran = 0;
        bool bos = false, eos = false;
        int r = demuxer.read_packet(out.data(), out.size(), gran, bos, eos);
        assert(r == static_cast<int>(large_pkt.size()));
        assert(bos == true);
        assert(eos == true);
        assert(gran == 99999);
        assert(std::memcmp(out.data(), large_pkt.data(), large_pkt.size()) == 0);
    }

    // 5. Byte-by-byte streaming feed
    {
        OggMuxer muxer(0x11223344);
        uint8_t data[] = {10, 20, 30, 40, 50};
        assert(muxer.write_packet(data, sizeof(data), true, true, 100));

        uint8_t page_buf[512];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        for (int i = 0; i < page_len; ++i) {
            size_t consumed = 0;
            assert(demuxer.push_bytes(&page_buf[i], 1, consumed));
            assert(consumed == 1);
        }

        uint8_t out[64];
        int64_t gran = 0;
        bool bos = false, eos = false;
        int r = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(r == sizeof(data));
        assert(std::memcmp(out, data, sizeof(data)) == 0);
        assert(bos == true);
        assert(eos == true);
        assert(gran == 100);
    }

    // 6. Resync after corrupted/junk bytes
    {
        OggMuxer muxer(0xCAFEBABE);
        uint8_t payload[] = {'V', 'O', 'R', 'B', 'I', 'S'};
        assert(muxer.write_packet(payload, sizeof(payload), true, true, 42));

        uint8_t page_buf[512];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        // Prepend 20 bytes of garbage
        std::vector<uint8_t> corrupted;
        for (int i = 0; i < 20; ++i) corrupted.push_back(static_cast<uint8_t>(i + 1));
        corrupted.insert(corrupted.end(), page_buf, page_buf + page_len);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(corrupted.data(), corrupted.size(), consumed));

        uint8_t out[64];
        int64_t gran = 0;
        bool bos = false, eos = false;
        int r = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(r == sizeof(payload));
        assert(std::memcmp(out, payload, sizeof(payload)) == 0);
        assert(bos == true);
        assert(eos == true);
        assert(gran == 42);
    }

    // 7. CRC corruption rejection
    {
        OggMuxer muxer(0x1337BEEF);
        uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
        assert(muxer.write_packet(payload, sizeof(payload), true, true, 100));

        uint8_t page_buf[512];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        // Corrupt 1 payload byte
        page_buf[page_len - 1] ^= 0xFF;

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));

        uint8_t out[64];
        int64_t gran = 0;
        bool bos = false, eos = false;
        int r = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        // Due to CRC mismatch, corrupted page must be rejected
        assert(r == 0);
    }

    // 8. Empty packet (0-byte payload)
    {
        OggMuxer muxer(0x55555555);
        assert(muxer.write_packet(nullptr, 0, true, true, 0));

        uint8_t page_buf[512];
        int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
        assert(page_len > 0);

        OggDemuxer demuxer;
        size_t consumed = 0;
        assert(demuxer.push_bytes(page_buf, page_len, consumed));

        uint8_t out[64];
        int64_t gran = 0;
        bool bos = false, eos = false;
        int r = demuxer.read_packet(out, sizeof(out), gran, bos, eos);
        assert(r == 0);
        assert(bos == true);
        assert(eos == true);
    }

    std::cout << "Ogg framing tests passed!\n";
    return 0;
}
