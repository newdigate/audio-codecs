#include "src/aac/encoder/huffman_encoder.h"
#include "src/aac/decoder/huffman_decoder.h"
#include <algorithm>
#include <cmath>
#include <climits>

namespace audio_codecs::aac {

bool AacHuffmanEncoder::encode_spectral_quad(core::BitWriter& writer, int codebook, const int* quad) {
    if (!quad || codebook < 1 || codebook > 4) return false;

    int w = quad[0];
    int x = quad[1];
    int y = quad[2];
    int z = quad[3];

    if (codebook == 1 || codebook == 2) {
        // Signed, LAV = 1
        if (std::abs(w) > 1 || std::abs(x) > 1 || std::abs(y) > 1 || std::abs(z) > 1) return false;
        int idx = (w + 1) * 27 + (x + 1) * 9 + (y + 1) * 3 + (z + 1);
        uint32_t code = 0;
        uint8_t len = 0;
        if (!get_huffman_code(codebook, idx, code, len)) return false;
        writer.write_bits(code, len);
        return true;
    } else if (codebook == 3 || codebook == 4) {
        // Unsigned, LAV = 2
        int aw = std::abs(w);
        int ax = std::abs(x);
        int ay = std::abs(y);
        int az = std::abs(z);
        if (aw > 2 || ax > 2 || ay > 2 || az > 2) return false;

        int idx = aw * 27 + ax * 9 + ay * 3 + az;
        uint32_t code = 0;
        uint8_t len = 0;
        if (!get_huffman_code(codebook, idx, code, len)) return false;
        writer.write_bits(code, len);

        if (aw != 0) writer.write_bits((w < 0) ? 1 : 0, 1);
        if (ax != 0) writer.write_bits((x < 0) ? 1 : 0, 1);
        if (ay != 0) writer.write_bits((y < 0) ? 1 : 0, 1);
        if (az != 0) writer.write_bits((z < 0) ? 1 : 0, 1);
        return true;
    }

    return false;
}

bool AacHuffmanEncoder::encode_spectral_pair(core::BitWriter& writer, int codebook, const int* pair) {
    if (!pair || codebook < 5 || codebook > 11) return false;

    int y = pair[0];
    int z = pair[1];

    if (codebook == 5 || codebook == 6) {
        // Signed, LAV = 4
        if (std::abs(y) > 4 || std::abs(z) > 4) return false;
        int idx = (y + 4) * 9 + (z + 4);
        uint32_t code = 0;
        uint8_t len = 0;
        if (!get_huffman_code(codebook, idx, code, len)) return false;
        writer.write_bits(code, len);
        return true;
    } else if (codebook == 7 || codebook == 8) {
        // Unsigned, LAV = 7
        int ay = std::abs(y);
        int az = std::abs(z);
        if (ay > 7 || az > 7) return false;
        int idx = ay * 8 + az;
        uint32_t code = 0;
        uint8_t len = 0;
        if (!get_huffman_code(codebook, idx, code, len)) return false;
        writer.write_bits(code, len);
        if (ay != 0) writer.write_bits((y < 0) ? 1 : 0, 1);
        if (az != 0) writer.write_bits((z < 0) ? 1 : 0, 1);
        return true;
    } else if (codebook == 9 || codebook == 10) {
        // Unsigned, LAV = 12
        int ay = std::abs(y);
        int az = std::abs(z);
        if (ay > 12 || az > 12) return false;
        int idx = ay * 13 + az;
        uint32_t code = 0;
        uint8_t len = 0;
        if (!get_huffman_code(codebook, idx, code, len)) return false;
        writer.write_bits(code, len);
        if (ay != 0) writer.write_bits((y < 0) ? 1 : 0, 1);
        if (az != 0) writer.write_bits((z < 0) ? 1 : 0, 1);
        return true;
    } else if (codebook == 11) {
        // HCB_ESC, LAV = 16
        int ay = std::abs(y);
        int az = std::abs(z);
        int vy = std::min(ay, 16);
        int vz = std::min(az, 16);
        int idx = vy * 17 + vz;
        uint32_t code = 0;
        uint8_t len = 0;
        if (!get_huffman_code(11, idx, code, len)) return false;
        writer.write_bits(code, len);

        if (ay >= 16) {
            int N = 0;
            while ((1 << (N + 5)) <= ay) {
                N++;
            }
            for (int i = 0; i < N; ++i) {
                writer.write_bits(1, 1);
            }
            writer.write_bits(0, 1);
            uint32_t esc = static_cast<uint32_t>(ay - (1 << (N + 4)));
            writer.write_bits(esc, N + 4);
        }

        if (az >= 16) {
            int N = 0;
            while ((1 << (N + 5)) <= az) {
                N++;
            }
            for (int i = 0; i < N; ++i) {
                writer.write_bits(1, 1);
            }
            writer.write_bits(0, 1);
            uint32_t esc = static_cast<uint32_t>(az - (1 << (N + 4)));
            writer.write_bits(esc, N + 4);
        }

        if (ay != 0) writer.write_bits((y < 0) ? 1 : 0, 1);
        if (az != 0) writer.write_bits((z < 0) ? 1 : 0, 1);
        return true;
    }

    return false;
}

bool AacHuffmanEncoder::encode_scalefactor_delta(core::BitWriter& writer, int delta) {
    if (delta < -60 || delta > 60) return false;
    int idx = delta + 60;
    uint32_t code = 0;
    uint8_t len = 0;
    if (!get_huffman_code(HCB_SCALEFACTOR, idx, code, len)) return false;
    writer.write_bits(code, len);
    return true;
}

size_t AacHuffmanEncoder::count_bits_spectral_band(const int* quant_band, int len, int codebook) {
    if (!quant_band || len <= 0) return 0;
    if (codebook == HCB_ZERO) return 0;

    if (codebook >= 1 && codebook <= 4) {
        if (len % 4 != 0) return SIZE_MAX;
        size_t total = 0;
        for (int i = 0; i < len; i += 4) {
            int w = quant_band[i];
            int x = quant_band[i + 1];
            int y = quant_band[i + 2];
            int z = quant_band[i + 3];

            if (codebook == 1 || codebook == 2) {
                if (std::abs(w) > 1 || std::abs(x) > 1 || std::abs(y) > 1 || std::abs(z) > 1) return SIZE_MAX;
                int idx = (w + 1) * 27 + (x + 1) * 9 + (y + 1) * 3 + (z + 1);
                uint32_t code = 0;
                uint8_t clen = 0;
                if (!get_huffman_code(codebook, idx, code, clen)) return SIZE_MAX;
                total += clen;
            } else {
                int aw = std::abs(w);
                int ax = std::abs(x);
                int ay = std::abs(y);
                int az = std::abs(z);
                if (aw > 2 || ax > 2 || ay > 2 || az > 2) return SIZE_MAX;
                int idx = aw * 27 + ax * 9 + ay * 3 + az;
                uint32_t code = 0;
                uint8_t clen = 0;
                if (!get_huffman_code(codebook, idx, code, clen)) return SIZE_MAX;
                total += clen;
                if (aw != 0) total += 1;
                if (ax != 0) total += 1;
                if (ay != 0) total += 1;
                if (az != 0) total += 1;
            }
        }
        return total;
    } else if (codebook >= 5 && codebook <= 11) {
        if (len % 2 != 0) return SIZE_MAX;
        size_t total = 0;
        for (int i = 0; i < len; i += 2) {
            int y = quant_band[i];
            int z = quant_band[i + 1];

            if (codebook == 5 || codebook == 6) {
                if (std::abs(y) > 4 || std::abs(z) > 4) return SIZE_MAX;
                int idx = (y + 4) * 9 + (z + 4);
                uint32_t code = 0;
                uint8_t clen = 0;
                if (!get_huffman_code(codebook, idx, code, clen)) return SIZE_MAX;
                total += clen;
            } else if (codebook == 7 || codebook == 8) {
                int ay = std::abs(y);
                int az = std::abs(z);
                if (ay > 7 || az > 7) return SIZE_MAX;
                int idx = ay * 8 + az;
                uint32_t code = 0;
                uint8_t clen = 0;
                if (!get_huffman_code(codebook, idx, code, clen)) return SIZE_MAX;
                total += clen;
                if (ay != 0) total += 1;
                if (az != 0) total += 1;
            } else if (codebook == 9 || codebook == 10) {
                int ay = std::abs(y);
                int az = std::abs(z);
                if (ay > 12 || az > 12) return SIZE_MAX;
                int idx = ay * 13 + az;
                uint32_t code = 0;
                uint8_t clen = 0;
                if (!get_huffman_code(codebook, idx, code, clen)) return SIZE_MAX;
                total += clen;
                if (ay != 0) total += 1;
                if (az != 0) total += 1;
            } else if (codebook == 11) {
                int ay = std::abs(y);
                int az = std::abs(z);
                int vy = std::min(ay, 16);
                int vz = std::min(az, 16);
                int idx = vy * 17 + vz;
                uint32_t code = 0;
                uint8_t clen = 0;
                if (!get_huffman_code(11, idx, code, clen)) return SIZE_MAX;
                total += clen;
                if (ay >= 16) {
                    int N = 0;
                    while ((1 << (N + 5)) <= ay) N++;
                    total += N + 1 + (N + 4);
                }
                if (az >= 16) {
                    int N = 0;
                    while ((1 << (N + 5)) <= az) N++;
                    total += N + 1 + (N + 4);
                }
                if (ay != 0) total += 1;
                if (az != 0) total += 1;
            }
        }
        return total;
    }

    return SIZE_MAX;
}

int AacHuffmanEncoder::find_best_codebook_for_band(const int* quant_band, int len) {
    if (!quant_band || len <= 0) return HCB_ZERO;

    bool all_zero = true;
    for (int i = 0; i < len; ++i) {
        if (quant_band[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) return HCB_ZERO;

    int best_cb = HCB_ESC;
    size_t min_bits = SIZE_MAX;

    for (int cb = 1; cb <= 11; ++cb) {
        size_t bits = count_bits_spectral_band(quant_band, len, cb);
        if (bits < min_bits) {
            min_bits = bits;
            best_cb = cb;
        }
    }

    return best_cb;
}

size_t AacHuffmanEncoder::encode_spectral_data_band(core::BitWriter& writer, int codebook, const int* quant, int len) {
    if (!quant || len <= 0 || codebook == HCB_ZERO) return 0;

    size_t start_bits = writer.get_bit_count();

    if (codebook >= 1 && codebook <= 4) {
        for (int i = 0; i < len; i += 4) {
            encode_spectral_quad(writer, codebook, &quant[i]);
        }
    } else if (codebook >= 5 && codebook <= 11) {
        for (int i = 0; i < len; i += 2) {
            encode_spectral_pair(writer, codebook, &quant[i]);
        }
    }

    return writer.get_bit_count() - start_bits;
}

void AacHuffmanEncoder::build_sections(const int* band_codebooks, 
                                       size_t num_swb, 
                                       int* out_section_cb, 
                                       int* out_section_len, 
                                       size_t& out_num_sections) {
    out_num_sections = 0;
    if (!band_codebooks || num_swb == 0 || !out_section_cb || !out_section_len) return;

    int curr_cb = band_codebooks[0];
    int curr_len = 1;

    for (size_t b = 1; b < num_swb; ++b) {
        if (band_codebooks[b] == curr_cb) {
            curr_len++;
        } else {
            out_section_cb[out_num_sections] = curr_cb;
            out_section_len[out_num_sections] = curr_len;
            out_num_sections++;
            curr_cb = band_codebooks[b];
            curr_len = 1;
        }
    }

    out_section_cb[out_num_sections] = curr_cb;
    out_section_len[out_num_sections] = curr_len;
    out_num_sections++;
}

size_t AacHuffmanEncoder::write_section_data(core::BitWriter& writer, 
                                             const int* section_codebooks, 
                                             const int* section_lengths, 
                                             size_t num_sections) {
    if (!section_codebooks || !section_lengths || num_sections == 0) return 0;

    size_t start_bits = writer.get_bit_count();

    for (size_t s = 0; s < num_sections; ++s) {
        int cb = section_codebooks[s];
        int len = section_lengths[s];

        writer.write_bits(static_cast<uint32_t>(cb), 4);
        while (len >= 31) {
            writer.write_bits(31, 5);
            len -= 31;
        }
        writer.write_bits(static_cast<uint32_t>(len), 5);
    }

    return writer.get_bit_count() - start_bits;
}

size_t AacHuffmanEncoder::write_scalefactor_data(core::BitWriter& writer, 
                                                 int global_gain, 
                                                 const int* scalefactors, 
                                                 const int* section_codebooks, 
                                                 size_t num_swb) {
    if (!scalefactors || !section_codebooks || num_swb == 0) return 0;

    size_t start_bits = writer.get_bit_count();
    int last_sf = global_gain;

    for (size_t b = 0; b < num_swb; ++b) {
        int cb = section_codebooks[b];
        if (cb == HCB_ZERO) {
            continue;
        }

        int sf = scalefactors[b];
        int delta = sf - last_sf;
        if (delta > 60) delta = 60;
        if (delta < -60) delta = -60;

        encode_scalefactor_delta(writer, delta);
        last_sf = sf;
    }

    return writer.get_bit_count() - start_bits;
}

size_t AacHuffmanEncoder::write_spectral_data(core::BitWriter& writer, 
                                              const int* quant_spectral, 
                                              const int* section_codebooks, 
                                              const int* swb_offsets, 
                                              size_t num_swb) {
    if (!quant_spectral || !section_codebooks || !swb_offsets || num_swb == 0) return 0;

    size_t start_bits = writer.get_bit_count();

    for (size_t b = 0; b < num_swb; ++b) {
        int cb = section_codebooks[b];
        int start = swb_offsets[b];
        int end = swb_offsets[b + 1];
        int width = end - start;

        encode_spectral_data_band(writer, cb, &quant_spectral[start], width);
    }

    return writer.get_bit_count() - start_bits;
}

} // namespace audio_codecs::aac
