#include "src/mp3/encoder/huffman_encoder.h"
#include <algorithm>
#include <cmath>

namespace audio_codecs::mp3 {

static const HuffmanEntry* find_entry(const HuffmanCodebook& book, uint8_t x, uint8_t y) {
    if (!book.entries) return nullptr;
    for (size_t i = 0; i < book.num_entries; ++i) {
        if (book.entries[i].x == x && book.entries[i].y == y && book.entries[i].len > 0) {
            return &book.entries[i];
        }
    }
    return nullptr;
}

int HuffmanEncoder::choose_optimal_table(const int16_t* is, int len) {
    if (!is || len <= 0) return 0;

    int max_val = 0;
    for (int i = 0; i < len; ++i) {
        max_val = std::max(max_val, static_cast<int>(std::abs(is[i])));
    }

    if (max_val == 0) return 0;

    // Filter candidate tables
    int best_table = 1;
    size_t min_bits = static_cast<size_t>(-1);

    // Standard tables: 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16..31
    static const int CANDIDATE_TABLES[] = {
        1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
    };

    for (int t : CANDIDATE_TABLES) {
        const HuffmanCodebook& book = HUFFMAN_CODEBOOKS[t];
        int max_supported = (book.linbits == 0) ? book.max_val : (15 + ((1 << book.linbits) - 1));
        if (max_val > max_supported) continue;

        size_t bits = count_bits_pairs(is, len, t);
        if (bits < min_bits) {
            min_bits = bits;
            best_table = t;
        }
    }

    return best_table;
}

size_t HuffmanEncoder::count_bits_pairs(const int16_t* is, int len, int table_num) {
    if (!is || len <= 0 || table_num == 0) return 0;
    const HuffmanCodebook& book = HUFFMAN_CODEBOOKS[table_num];
    size_t total_bits = 0;

    for (int i = 0; i + 1 < len; i += 2) {
        int abs_x = std::abs(is[i]);
        int abs_y = std::abs(is[i + 1]);

        uint8_t lookup_x = std::min(abs_x, 15);
        uint8_t lookup_y = std::min(abs_y, 15);

        const HuffmanEntry* entry = find_entry(book, lookup_x, lookup_y);
        if (!entry) return static_cast<size_t>(-1);

        total_bits += entry->len;

        if (lookup_x == 15 && book.linbits > 0) total_bits += book.linbits;
        if (abs_x != 0) total_bits += 1;

        if (lookup_y == 15 && book.linbits > 0) total_bits += book.linbits;
        if (abs_y != 0) total_bits += 1;
    }

    return total_bits;
}

void HuffmanEncoder::encode_pairs(core::BitWriter& writer, const int16_t* is, int len, int table_num) {
    if (!is || len <= 0 || table_num == 0) return;
    const HuffmanCodebook& book = HUFFMAN_CODEBOOKS[table_num];

    for (int i = 0; i + 1 < len; i += 2) {
        int val_x = is[i];
        int val_y = is[i + 1];
        int abs_x = std::abs(val_x);
        int abs_y = std::abs(val_y);

        uint8_t lookup_x = std::min(abs_x, 15);
        uint8_t lookup_y = std::min(abs_y, 15);

        const HuffmanEntry* entry = find_entry(book, lookup_x, lookup_y);
        if (!entry) continue;

        writer.write_bits(entry->code, entry->len);

        if (lookup_x == 15 && book.linbits > 0) {
            uint32_t lin = static_cast<uint32_t>(abs_x - 15);
            writer.write_bits(lin, book.linbits);
        }
        if (abs_x != 0) {
            writer.write_bits((val_x < 0) ? 1 : 0, 1);
        }

        if (lookup_y == 15 && book.linbits > 0) {
            uint32_t lin = static_cast<uint32_t>(abs_y - 15);
            writer.write_bits(lin, book.linbits);
        }
        if (abs_y != 0) {
            writer.write_bits((val_y < 0) ? 1 : 0, 1);
        }
    }
}

void HuffmanEncoder::encode_count1(core::BitWriter& writer, const int16_t* is, int num_quads, bool table_b) {
    if (!is || num_quads <= 0) return;

    for (int q = 0; q < num_quads; ++q) {
        const int16_t* quad = &is[q * 4];
        uint8_t v = (quad[0] != 0) ? 1 : 0;
        uint8_t w = (quad[1] != 0) ? 1 : 0;
        uint8_t x = (quad[2] != 0) ? 1 : 0;
        uint8_t y = (quad[3] != 0) ? 1 : 0;

        if (!table_b) {
            for (int i = 0; i < 16; ++i) {
                const Count1Entry& e = COUNT1_TABLE_A[i];
                if (e.v == v && e.w == w && e.x == x && e.y == y) {
                    writer.write_bits(e.code, e.len);
                    break;
                }
            }
        } else {
            uint32_t word = ((v ? 0 : 1) << 3) | ((w ? 0 : 1) << 2) | ((x ? 0 : 1) << 1) | (y ? 0 : 1);
            writer.write_bits(word, 4);
        }

        if (quad[0] != 0) writer.write_bits((quad[0] < 0) ? 1 : 0, 1);
        if (quad[1] != 0) writer.write_bits((quad[1] < 0) ? 1 : 0, 1);
        if (quad[2] != 0) writer.write_bits((quad[2] < 0) ? 1 : 0, 1);
        if (quad[3] != 0) writer.write_bits((quad[3] < 0) ? 1 : 0, 1);
    }
}

size_t HuffmanEncoder::count_bits_count1(const int16_t* is, int num_quads, bool table_b) {
    if (!is || num_quads <= 0) return 0;
    size_t bits = 0;

    for (int q = 0; q < num_quads; ++q) {
        const int16_t* quad = &is[q * 4];
        uint8_t v = (quad[0] != 0) ? 1 : 0;
        uint8_t w = (quad[1] != 0) ? 1 : 0;
        uint8_t x = (quad[2] != 0) ? 1 : 0;
        uint8_t y = (quad[3] != 0) ? 1 : 0;

        if (!table_b) {
            for (int i = 0; i < 16; ++i) {
                const Count1Entry& e = COUNT1_TABLE_A[i];
                if (e.v == v && e.w == w && e.x == x && e.y == y) {
                    bits += e.len;
                    break;
                }
            }
        } else {
            bits += 4;
        }

        if (quad[0] != 0) bits += 1;
        if (quad[1] != 0) bits += 1;
        if (quad[2] != 0) bits += 1;
        if (quad[3] != 0) bits += 1;
    }

    return bits;
}

} // namespace audio_codecs::mp3
