#include "src/aac/decoder/huffman_decoder.h"
#include "src/aac/decoder/requantizer.h"
#include "src/aac/aac_tables.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_requantizer_basic() {
    using namespace audio_codecs::aac;

    // Test non-linear dequantization formula:
    // X_inv = sign(X) * |X|^(4/3) * 2^(0.25 * (sf - 100))
    int quant[8] = {0, 1, -1, 8, -8, 27, -27, 64};
    int sf[1] = {100}; // 2^0 = 1.0
    int swb[2] = {0, 8};
    float out[8] = {0.0f};

    requantize_spectrum(quant, sf, swb, 1, out);

    assert(std::fabs(out[0] - 0.0f) < 1e-4f);
    assert(std::fabs(out[1] - 1.0f) < 1e-4f);
    assert(std::fabs(out[2] - (-1.0f)) < 1e-4f);
    assert(std::fabs(out[3] - 16.0f) < 1e-4f);
    assert(std::fabs(out[4] - (-16.0f)) < 1e-4f);
    assert(std::fabs(out[5] - 81.0f) < 1e-4f);
    assert(std::fabs(out[6] - (-81.0f)) < 1e-4f);
    assert(std::fabs(out[7] - 256.0f) < 1e-4f);
}

void test_requantizer_scalefactor_scaling() {
    using namespace audio_codecs::aac;

    int quant[4] = {8, 8, 8, 8};
    // 8^(4/3) = 16.0
    // sf = 100: scale = 2^0 = 1.0 -> 16.0
    // sf = 104: scale = 2^1 = 2.0 -> 32.0
    // sf = 96:  scale = 2^(-1) = 0.5 -> 8.0
    // sf = 108: scale = 2^2 = 4.0 -> 64.0
    int sf[4] = {100, 104, 96, 108};
    int swb[5] = {0, 1, 2, 3, 4};
    float out[4] = {0.0f};

    requantize_spectrum(quant, sf, swb, 4, out);

    assert(std::fabs(out[0] - 16.0f) < 1e-4f);
    assert(std::fabs(out[1] - 32.0f) < 1e-4f);
    assert(std::fabs(out[2] - 8.0f) < 1e-4f);
    assert(std::fabs(out[3] - 64.0f) < 1e-4f);
}

void test_requantizer_short_spectrum() {
    using namespace audio_codecs::aac;

    // 8 windows, 2 SWBs per window, SWB offsets: {0, 64, 128}
    size_t num_windows = 8;
    size_t num_swb = 2;
    int swb_short[3] = {0, 64, 128};

    std::vector<int> quant(num_windows * 128, 0);
    std::vector<int> sf(num_windows * num_swb, 100);
    std::vector<float> out(num_windows * 128, 0.0f);

    // Set some distinct values in window 0 and window 7
    quant[0] = 8;        // window 0, band 0 -> sf[0] = 100 -> 16.0
    quant[64] = 8;       // window 0, band 1 -> sf[1] = 104 -> 32.0
    sf[1] = 104;

    quant[7 * 128 + 0] = -27; // window 7, band 0 -> sf[7*2+0] = 96 -> -81.0 * 0.5 = -40.5
    sf[7 * 2 + 0] = 96;

    requantize_short_spectrum(quant.data(), sf.data(), swb_short, num_swb, num_windows, out.data());

    assert(std::fabs(out[0] - 16.0f) < 1e-4f);
    assert(std::fabs(out[64] - 32.0f) < 1e-4f);
    assert(std::fabs(out[7 * 128 + 0] - (-40.5f)) < 1e-3f);
    assert(std::fabs(out[1] - 0.0f) < 1e-6f);
}

void test_scalefactor_dpcm_decoding() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    std::vector<int> test_deltas = {0, 1, -1, 2, -2, 5, -5, 10, -10, 30, -30, 60, -60};
    std::vector<uint8_t> buffer(512, 0);

    BitWriter writer;
    writer.init(buffer.data(), buffer.size());
    for (int delta : test_deltas) {
        int idx = delta + 60;
        uint32_t code = 0;
        uint8_t len = 0;
        bool ok = get_huffman_code(12, idx, code, len);
        assert(ok);
        writer.write_bits(code, len);
    }
    writer.flush_to_byte();

    BitReader reader;
    reader.init(buffer.data(), writer.get_byte_count());

    for (int expected_delta : test_deltas) {
        int decoded_delta = 999;
        bool ok = decode_scalefactor_delta(reader, decoded_delta);
        assert(ok);
        assert(decoded_delta == expected_delta);
    }
}

void test_spectral_quad_codebooks() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    // Test CB 1 (signed, dim=4, max=1)
    {
        std::vector<uint8_t> buf(256, 0);
        BitWriter writer;
        writer.init(buf.data(), buf.size());

        // Quad 1: (0, 0, 0, 0) -> idx = (1)*27 + (1)*9 + (1)*3 + 1 = 40
        uint32_t c0; uint8_t l0;
        get_huffman_code(1, 40, c0, l0);
        writer.write_bits(c0, l0);

        // Quad 2: (1, -1, 0, 1) -> idx = 2*27 + 0*9 + 1*3 + 2 = 54 + 0 + 3 + 2 = 59
        uint32_t c1; uint8_t l1;
        get_huffman_code(1, 59, c1, l1);
        writer.write_bits(c1, l1);

        writer.flush_to_byte();

        BitReader reader;
        reader.init(buf.data(), writer.get_byte_count());

        int quad[4] = {0};
        assert(decode_spectral_quad(reader, 1, quad));
        assert(quad[0] == 0 && quad[1] == 0 && quad[2] == 0 && quad[3] == 0);

        assert(decode_spectral_quad(reader, 1, quad));
        assert(quad[0] == 1 && quad[1] == -1 && quad[2] == 0 && quad[3] == 1);
    }

    // Test CB 3 (unsigned, dim=4, max=2, sign bits)
    {
        std::vector<uint8_t> buf(256, 0);
        BitWriter writer;
        writer.init(buf.data(), buf.size());

        // Quad 1: (2, -1, 0, -2) -> unsigned (2, 1, 0, 2)
        // idx = 2*27 + 1*9 + 0*3 + 2 = 54 + 9 + 0 + 2 = 65
        uint32_t c0; uint8_t l0;
        get_huffman_code(3, 65, c0, l0);
        writer.write_bits(c0, l0);
        // Sign bits: w=2 (>0, sign=0 pos), x=1 (>0, sign=1 neg), y=0 (no sign), z=2 (>0, sign=1 neg)
        writer.write_bits(0, 1); // w pos
        writer.write_bits(1, 1); // x neg
        writer.write_bits(1, 1); // z neg

        writer.flush_to_byte();

        BitReader reader;
        reader.init(buf.data(), writer.get_byte_count());

        int quad[4] = {0};
        assert(decode_spectral_quad(reader, 3, quad));
        assert(quad[0] == 2);
        assert(quad[1] == -1);
        assert(quad[2] == 0);
        assert(quad[3] == -2);
    }
}

void test_spectral_pair_codebooks() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    // Test CB 5 (signed, dim=2, max=4)
    {
        std::vector<uint8_t> buf(256, 0);
        BitWriter writer;
        writer.init(buf.data(), buf.size());

        // Pair: (-4, 3) -> idx = (-4 + 4)*9 + (3 + 4) = 0*9 + 7 = 7
        uint32_t c0; uint8_t l0;
        get_huffman_code(5, 7, c0, l0);
        writer.write_bits(c0, l0);

        writer.flush_to_byte();

        BitReader reader;
        reader.init(buf.data(), writer.get_byte_count());

        int pair[2] = {0};
        assert(decode_spectral_pair(reader, 5, pair));
        assert(pair[0] == -4 && pair[1] == 3);
    }

    // Test CB 7 (unsigned, dim=2, max=7, sign bits)
    {
        std::vector<uint8_t> buf(256, 0);
        BitWriter writer;
        writer.init(buf.data(), buf.size());

        // Pair: (-7, 5) -> unsigned (7, 5) -> idx = 7*8 + 5 = 61
        uint32_t c0; uint8_t l0;
        get_huffman_code(7, 61, c0, l0);
        writer.write_bits(c0, l0);
        writer.write_bits(1, 1); // y neg
        writer.write_bits(0, 1); // z pos

        writer.flush_to_byte();

        BitReader reader;
        reader.init(buf.data(), writer.get_byte_count());

        int pair[2] = {0};
        assert(decode_spectral_pair(reader, 7, pair));
        assert(pair[0] == -7 && pair[1] == 5);
    }

    // Test CB 9 (unsigned, dim=2, max=12, sign bits)
    {
        std::vector<uint8_t> buf(256, 0);
        BitWriter writer;
        writer.init(buf.data(), buf.size());

        // Pair: (12, -10) -> unsigned (12, 10) -> idx = 12*13 + 10 = 166
        uint32_t c0; uint8_t l0;
        get_huffman_code(9, 166, c0, l0);
        writer.write_bits(c0, l0);
        writer.write_bits(0, 1); // y pos
        writer.write_bits(1, 1); // z neg

        writer.flush_to_byte();

        BitReader reader;
        reader.init(buf.data(), writer.get_byte_count());

        int pair[2] = {0};
        assert(decode_spectral_pair(reader, 9, pair));
        assert(pair[0] == 12 && pair[1] == -10);
    }
}

void test_codebook_11_escape() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    std::vector<uint8_t> buf(256, 0);
    BitWriter writer;
    writer.init(buf.data(), buf.size());

    // Pair 1: (16, 16) with escape sequences:
    // y = -25: esc for 16 -> N=0 ('0'), 4 bits value 9 (16 + 9 = 25), sign bit 1 (neg)
    // z = +45: esc for 16 -> N=1 ('10'), 5 bits value 13 (32 + 13 = 45), sign bit 0 (pos)
    // idx = 16*17 + 16 = 288
    uint32_t c0; uint8_t l0;
    get_huffman_code(11, 288, c0, l0);
    writer.write_bits(c0, l0);

    // Escape for y: N=0 -> bit '0', then 4 bits = 9 (0b1001)
    writer.write_bits(0, 1);
    writer.write_bits(9, 4);

    // Escape for z: N=1 -> bits '10', then 5 bits = 13 (0b01101)
    writer.write_bits(1, 1);
    writer.write_bits(0, 1);
    writer.write_bits(13, 5);

    // Sign bits: y is 25 (>0, sign=1), z is 45 (>0, sign=0)
    writer.write_bits(1, 1); // y neg
    writer.write_bits(0, 1); // z pos

    // Pair 2: (7, -16) where only z triggers escape:
    // y = 7 (no escape)
    // z = -150: esc for 16 -> N=3 ('1110'), 7 bits value (150 - 128 = 22 = 0b0010110)
    // idx = 7*17 + 16 = 135
    uint32_t c1; uint8_t l1;
    get_huffman_code(11, 135, c1, l1);
    writer.write_bits(c1, l1);

    // Escape for z: N=3 ('1110'), then 7 bits = 22
    writer.write_bits(1, 1);
    writer.write_bits(1, 1);
    writer.write_bits(1, 1);
    writer.write_bits(0, 1);
    writer.write_bits(22, 7);

    // Sign bits: y=7 (pos, sign=0), z=150 (neg, sign=1)
    writer.write_bits(0, 1);
    writer.write_bits(1, 1);

    writer.flush_to_byte();

    BitReader reader;
    reader.init(buf.data(), writer.get_byte_count());

    int pair[2] = {0};
    assert(decode_spectral_pair(reader, 11, pair));
    assert(pair[0] == -25);
    assert(pair[1] == 45);

    assert(decode_spectral_pair(reader, 11, pair));
    assert(pair[0] == 7);
    assert(pair[1] == -150);
}

void test_decode_spectral_data_bulk() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    std::vector<uint8_t> buf(512, 0);
    BitWriter writer;
    writer.init(buf.data(), buf.size());

    // Write 8 coefficients using CB 1 (2 quads: (0,0,0,0) and (1,0,-1,0))
    uint32_t c0; uint8_t l0;
    get_huffman_code(1, 40, c0, l0); // (0,0,0,0)
    writer.write_bits(c0, l0);

    // (1,0,-1,0) -> 2*27 + 1*9 + 0*3 + 1 = 54 + 9 + 0 + 1 = 64
    uint32_t c1; uint8_t l1;
    get_huffman_code(1, 64, c1, l1);
    writer.write_bits(c1, l1);

    writer.flush_to_byte();

    BitReader reader;
    reader.init(buf.data(), writer.get_byte_count());

    int out_spec[16] = {0};
    assert(decode_spectral_data(reader, 1, out_spec, 8));
    assert(out_spec[0] == 0 && out_spec[1] == 0 && out_spec[2] == 0 && out_spec[3] == 0);
    assert(out_spec[4] == 1 && out_spec[5] == 0 && out_spec[6] == -1 && out_spec[7] == 0);

    // Test CB 0 (zero book)
    int zero_spec[16] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(decode_spectral_data(reader, HCB_ZERO, zero_spec, 8));
    for (int i = 0; i < 8; ++i) {
        assert(zero_spec[i] == 0);
    }
}

void test_all_codebooks_roundtrip() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    for (int cb = 1; cb <= 11; ++cb) {
        int dim = get_codebook_dimension(cb);
        int size = get_codebook_size(cb);
        assert(dim == 2 || dim == 4);
        assert(size > 0);

        std::vector<uint8_t> buf(1024, 0);
        BitWriter writer;
        writer.init(buf.data(), buf.size());

        // Write zero index and last index of each book
        uint32_t c_first, c_last;
        uint8_t l_first, l_last;
        assert(get_huffman_code(cb, 0, c_first, l_first));
        assert(get_huffman_code(cb, size - 1, c_last, l_last));

        writer.write_bits(c_first, l_first);
        if (!is_codebook_signed(cb)) {
            // Unsigned book: if non-zero components, write positive sign bits
            int tup[4] = {0};
            // idx 0 is all zeros for unsigned books
        }

        writer.write_bits(c_last, l_last);
        if (!is_codebook_signed(cb)) {
            // For last idx, components are at LAV, write positive sign bits (0)
            if (cb != 11) {
                for (int d = 0; d < dim; ++d) {
                    writer.write_bits(0, 1);
                }
            } else {
                // CB 11 idx 288 is (16, 16). Esc values:
                // y: N=0 (0), 4 bits (0) -> 16 + 0 = 16
                writer.write_bits(0, 1);
                writer.write_bits(0, 4);
                // z: N=0 (0), 4 bits (0) -> 16 + 0 = 16
                writer.write_bits(0, 1);
                writer.write_bits(0, 4);
                // signs
                writer.write_bits(0, 1);
                writer.write_bits(0, 1);
            }
        }
        writer.flush_to_byte();

        BitReader reader;
        reader.init(buf.data(), writer.get_byte_count());

        int out[4] = {0};
        if (dim == 4) {
            assert(decode_spectral_quad(reader, cb, out));
            assert(decode_spectral_quad(reader, cb, out));
        } else {
            assert(decode_spectral_pair(reader, cb, out));
            assert(decode_spectral_pair(reader, cb, out));
        }
    }
}

int main() {
    std::cout << "Testing AAC Huffman Spectral Decoder & Requantizer...\n";
    test_requantizer_basic();
    test_requantizer_scalefactor_scaling();
    test_requantizer_short_spectrum();
    test_scalefactor_dpcm_decoding();
    test_spectral_quad_codebooks();
    test_spectral_pair_codebooks();
    test_codebook_11_escape();
    test_decode_spectral_data_bulk();
    test_all_codebooks_roundtrip();
    std::cout << "All AAC Huffman & Requantizer tests passed successfully!\n";
    return 0;
}
