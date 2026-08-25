#include "src/aac/decoder/huffman_decoder.h"
#include <vector>

namespace audio_codecs::aac {

struct HuffmanCodeEntry {
    uint32_t code;
    uint8_t len;
};

struct HuffmanTreeNode {
    int16_t child[2]; // >= 0: node index, < 0: ~leaf_symbol (-(symbol + 1))
};

#include "src/aac/decoder/huffman_tables_data.inc"

namespace {

struct CodebookMetadata {
    const HuffmanCodeEntry* table{nullptr};
    int num_entries{0};
    int dim{0};
    int lav{0};
    bool is_signed{false};
};

struct AacHuffmanContainer {
    CodebookMetadata meta[13];
    std::vector<HuffmanTreeNode> trees[13];

    AacHuffmanContainer() {
        meta[1]  = { CB_1_TABLE,  81,  4, 1,  true };
        meta[2]  = { CB_2_TABLE,  81,  4, 1,  true };
        meta[3]  = { CB_3_TABLE,  81,  4, 2,  false };
        meta[4]  = { CB_4_TABLE,  81,  4, 2,  false };
        meta[5]  = { CB_5_TABLE,  81,  2, 4,  true };
        meta[6]  = { CB_6_TABLE,  81,  2, 4,  true };
        meta[7]  = { CB_7_TABLE,  64,  2, 7,  false };
        meta[8]  = { CB_8_TABLE,  64,  2, 7,  false };
        meta[9]  = { CB_9_TABLE,  169, 2, 12, false };
        meta[10] = { CB_10_TABLE, 169, 2, 12, false };
        meta[11] = { CB_11_TABLE, 289, 2, 16, false };
        meta[12] = { CB_12_TABLE, 121, 1, 60, true }; // Scalefactor

        for (int cb = 1; cb <= 12; ++cb) {
            build_tree(cb);
        }
    }

    void build_tree(int cb) {
        trees[cb].clear();
        trees[cb].push_back({{-1, -1}}); // Root node at index 0

        int n_entries = meta[cb].num_entries;
        const HuffmanCodeEntry* table = meta[cb].table;

        for (int idx = 0; idx < n_entries; ++idx) {
            uint32_t code = table[idx].code;
            uint8_t len = table[idx].len;

            int node = 0;
            for (int b = len - 1; b >= 0; --b) {
                int bit = (code >> b) & 1;
                if (b == 0) {
                    trees[cb][node].child[bit] = static_cast<int16_t>(~idx); // Leaf node
                } else {
                    int16_t next = trees[cb][node].child[bit];
                    if (next < 0) {
                        int16_t new_node = static_cast<int16_t>(trees[cb].size());
                        trees[cb][node].child[bit] = new_node;
                        trees[cb].push_back({{-1, -1}});
                        node = new_node;
                    } else {
                        node = next;
                    }
                }
            }
        }
    }
};

static const AacHuffmanContainer g_huffman;

} // anonymous namespace

bool get_huffman_code(int codebook, int index, uint32_t& code, uint8_t& len) {
    if (codebook < 1 || codebook > 12) return false;
    if (index < 0 || index >= g_huffman.meta[codebook].num_entries) return false;

    code = g_huffman.meta[codebook].table[index].code;
    len = g_huffman.meta[codebook].table[index].len;
    return true;
}

int get_codebook_size(int codebook) {
    if (codebook >= 1 && codebook <= 12) {
        return g_huffman.meta[codebook].num_entries;
    }
    return 0;
}

int get_codebook_dimension(int codebook) {
    if (codebook >= 1 && codebook <= 12) {
        return g_huffman.meta[codebook].dim;
    }
    return 0;
}

bool is_codebook_signed(int codebook) {
    if (codebook >= 1 && codebook <= 12) {
        return g_huffman.meta[codebook].is_signed;
    }
    return false;
}

int get_codebook_lav(int codebook) {
    if (codebook >= 1 && codebook <= 12) {
        return g_huffman.meta[codebook].lav;
    }
    return 0;
}

bool decode_huffman_index(core::BitReader& reader, int codebook, int& out_index) {
    if (codebook < 1 || codebook > 12) return false;
    const auto& tree = g_huffman.trees[codebook];
    if (tree.empty()) return false;

    int node = 0;
    while (true) {
        if (reader.bits_remaining() == 0) {
            return false;
        }
        uint32_t bit = reader.read_bits(1);
        int16_t next = tree[node].child[bit];
        if (next < 0) {
            out_index = ~next;
            return true;
        }
        node = next;
    }
}

bool decode_spectral_quad(core::BitReader& reader, int codebook, int* out_quad) {
    if (!out_quad || codebook < 1 || codebook > 4) return false;

    int idx = 0;
    if (!decode_huffman_index(reader, codebook, idx)) return false;

    int lav = g_huffman.meta[codebook].lav;
    bool is_signed = g_huffman.meta[codebook].is_signed;
    int mod = is_signed ? (2 * lav + 1) : (lav + 1);
    int off = is_signed ? lav : 0;

    int w = (idx / (mod * mod * mod)) - off;
    int x = ((idx / (mod * mod)) % mod) - off;
    int y = ((idx / mod) % mod) - off;
    int z = (idx % mod) - off;

    if (!is_signed) {
        if (w != 0) {
            if (reader.bits_remaining() == 0) return false;
            if (reader.read_bits(1) == 1) w = -w;
        }
        if (x != 0) {
            if (reader.bits_remaining() == 0) return false;
            if (reader.read_bits(1) == 1) x = -x;
        }
        if (y != 0) {
            if (reader.bits_remaining() == 0) return false;
            if (reader.read_bits(1) == 1) y = -y;
        }
        if (z != 0) {
            if (reader.bits_remaining() == 0) return false;
            if (reader.read_bits(1) == 1) z = -z;
        }
    }

    out_quad[0] = w;
    out_quad[1] = x;
    out_quad[2] = y;
    out_quad[3] = z;
    return true;
}

bool decode_spectral_pair(core::BitReader& reader, int codebook, int* out_pair) {
    if (!out_pair || codebook < 5 || codebook > 11) return false;

    int idx = 0;
    if (!decode_huffman_index(reader, codebook, idx)) return false;

    int lav = g_huffman.meta[codebook].lav;
    bool is_signed = g_huffman.meta[codebook].is_signed;
    int mod = is_signed ? (2 * lav + 1) : (lav + 1);
    int off = is_signed ? lav : 0;

    int y = (idx / mod) - off;
    int z = (idx % mod) - off;

    if (codebook == HCB_ESC) {
        if (y == 16) {
            int N = 0;
            while (true) {
                if (reader.bits_remaining() == 0) return false;
                uint32_t b = reader.read_bits(1);
                if (b == 1) {
                    N++;
                } else {
                    break;
                }
            }
            if (reader.bits_remaining() < static_cast<size_t>(N + 4)) return false;
            uint32_t esc = reader.read_bits(N + 4);
            y = (1 << (N + 4)) + static_cast<int>(esc);
        }
        if (z == 16) {
            int N = 0;
            while (true) {
                if (reader.bits_remaining() == 0) return false;
                uint32_t b = reader.read_bits(1);
                if (b == 1) {
                    N++;
                } else {
                    break;
                }
            }
            if (reader.bits_remaining() < static_cast<size_t>(N + 4)) return false;
            uint32_t esc = reader.read_bits(N + 4);
            z = (1 << (N + 4)) + static_cast<int>(esc);
        }
    }

    if (!is_signed) {
        if (y != 0) {
            if (reader.bits_remaining() == 0) return false;
            if (reader.read_bits(1) == 1) y = -y;
        }
        if (z != 0) {
            if (reader.bits_remaining() == 0) return false;
            if (reader.read_bits(1) == 1) z = -z;
        }
    }

    out_pair[0] = y;
    out_pair[1] = z;
    return true;
}

bool decode_spectral_data(core::BitReader& reader, int codebook, int* out_spectral, int count) {
    if (count <= 0 || !out_spectral) return false;

    if (codebook == HCB_ZERO) {
        for (int i = 0; i < count; ++i) {
            out_spectral[i] = 0;
        }
        return true;
    }

    if (codebook >= 1 && codebook <= 4) {
        if (count % 4 != 0) return false;
        for (int i = 0; i < count; i += 4) {
            if (!decode_spectral_quad(reader, codebook, &out_spectral[i])) {
                return false;
            }
        }
        return true;
    } else if (codebook >= 5 && codebook <= 11) {
        if (count % 2 != 0) return false;
        for (int i = 0; i < count; i += 2) {
            if (!decode_spectral_pair(reader, codebook, &out_spectral[i])) {
                return false;
            }
        }
        return true;
    }

    return false;
}

bool decode_scalefactor_delta(core::BitReader& reader, int& out_delta) {
    int idx = 0;
    if (!decode_huffman_index(reader, HCB_SCALEFACTOR, idx)) {
        return false;
    }
    out_delta = idx - 60;
    return true;
}

} // namespace audio_codecs::aac
