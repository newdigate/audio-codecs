// tests/test_aiff_ieee80.cpp
#include "src/aiff/ieee80.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::aiff;

    // Test vector for 44100
    uint8_t b44100[10];
    uint32_to_ieee80(44100, b44100);
    // 44100 = 0xAC44 = 1.3458251953125 * 2^15
    // Exp = 16383 + 15 = 16398 = 0x400E
    assert(b44100[0] == 0x40 && b44100[1] == 0x0E);
    assert(b44100[2] == 0xAC && b44100[3] == 0x44);
    assert(b44100[4] == 0x00 && b44100[5] == 0x00);
    assert(b44100[6] == 0x00 && b44100[7] == 0x00);
    assert(b44100[8] == 0x00 && b44100[9] == 0x00);

    uint32_t decoded44100 = ieee80_to_uint32(b44100);
    assert(decoded44100 == 44100);

    // Test vector for 48000 (0xBB80 -> 1.46484375 * 2^15)
    uint8_t b48000[10];
    uint32_to_ieee80(48000, b48000);
    assert(b48000[0] == 0x40 && b48000[1] == 0x0E);
    assert(b48000[2] == 0xBB && b48000[3] == 0x80);
    assert(ieee80_to_uint32(b48000) == 48000);

    // Standard sample rates roundtrip
    const std::vector<uint32_t> standard_rates = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000,
        44100, 48000, 64000, 88200, 96000, 176400, 192000, 384000
    };

    for (uint32_t r : standard_rates) {
        uint8_t buf[10];
        uint32_to_ieee80(r, buf);
        uint32_t out_r = ieee80_to_uint32(buf);
        assert(out_r == r);
        double out_d = ieee80_to_double(buf);
        assert(std::round(out_d) == static_cast<double>(r));
    }

    // Custom sample rates
    const std::vector<uint32_t> custom_rates = { 1, 7, 1234, 12345, 65535, 100000, 999999 };
    for (uint32_t r : custom_rates) {
        uint8_t buf[10];
        uint32_to_ieee80(r, buf);
        uint32_t out_r = ieee80_to_uint32(buf);
        assert(out_r == r);
    }

    // Double conversion roundtrips
    double test_floats[] = { 44100.0, 48000.5, 22050.25, 8000.125, 0.5, 1.0, 100.0 };
    for (double v : test_floats) {
        uint8_t buf[10];
        double_to_ieee80(v, buf);
        double out_v = ieee80_to_double(buf);
        assert(std::abs(out_v - v) < 1e-9);
    }

    // Edge cases
    uint8_t zero_buf[10];
    uint32_to_ieee80(0, zero_buf);
    assert(ieee80_to_uint32(zero_buf) == 0);
    assert(ieee80_to_double(zero_buf) == 0.0);

    std::cout << "AIFF IEEE-80 float tests passed!\n";
    return 0;
}
