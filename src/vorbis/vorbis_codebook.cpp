#include "src/vorbis/vorbis_codebook.h"
#include "src/vorbis/vorbis_common.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::vorbis {

uint32_t vorbis_book_maptype1_quantvals(uint32_t entries, uint32_t dimensions) {
    if (dimensions == 0 || entries == 0) return 0;
    uint32_t vals = static_cast<uint32_t>(std::floor(std::pow(static_cast<double>(entries), 1.0 / static_cast<double>(dimensions))));
    while (true) {
        uint64_t acc = 1;
        bool overflow = false;
        for (size_t d = 0; d < dimensions; ++d) {
            acc *= (vals + 1);
            if (acc > entries) {
                overflow = true;
                break;
            }
        }
        if (!overflow && acc <= entries) {
            vals++;
        } else {
            break;
        }
    }
    return vals;
}

bool vorbis_codebook_init_tables(VorbisCodebook& book) {
    if (book.entries == 0 || book.dimensions == 0) return false;

    // 1. Calculate canonical Huffman codewords
    uint32_t len_count[33] = {0};
    for (uint8_t len : book.lengths) {
        if (len > 0 && len <= 32) {
            len_count[len]++;
        }
    }

    uint32_t next_code[33] = {0};
    uint32_t code = 0;
    for (int len = 1; len <= 32; ++len) {
        code = (code + len_count[len - 1]) << 1;
        next_code[len] = code;
    }

    book.codewords.assign(book.entries, 0);
    for (size_t i = 0; i < book.entries; ++i) {
        uint8_t len = (i < book.lengths.size()) ? book.lengths[i] : 0;
        if (len > 0) {
            book.codewords[i] = next_code[len]++;
        }
    }

    // 2. Build Huffman binary tree
    book.tree.clear();
    book.tree.push_back({-1, -1, -1});
    book.tree_root = 0;

    for (size_t entry = 0; entry < book.entries; ++entry) {
        uint8_t len = (entry < book.lengths.size()) ? book.lengths[entry] : 0;
        if (len == 0) continue;
        uint32_t c = book.codewords[entry];
        int16_t curr = book.tree_root;
        for (int bit_idx = len - 1; bit_idx >= 0; --bit_idx) {
            uint32_t bit = (c >> bit_idx) & 1;
            if (bit == 0) {
                if (book.tree[curr].left == -1) {
                    int16_t next = static_cast<int16_t>(book.tree.size());
                    book.tree.push_back({-1, -1, -1});
                    book.tree[curr].left = next;
                }
                curr = book.tree[curr].left;
            } else {
                if (book.tree[curr].right == -1) {
                    int16_t next = static_cast<int16_t>(book.tree.size());
                    book.tree.push_back({-1, -1, -1});
                    book.tree[curr].right = next;
                }
                curr = book.tree[curr].right;
            }
        }
        book.tree[curr].entry = static_cast<int32_t>(entry);
    }

    // 3. Compute valuelist lookup table if lookup_type > 0
    if (book.lookup_type == 1) {
        book.valuelist.assign(book.entries * book.dimensions, 0.0f);
        for (size_t i = 0; i < book.entries; ++i) {
            if (i < book.lengths.size() && book.lengths[i] == 0) continue;
            float last = 0.0f;
            uint32_t index_div = 1;
            for (size_t d = 0; d < book.dimensions; ++d) {
                uint32_t val_index = (book.lookup_values > 0) ? ((i / index_div) % book.lookup_values) : 0;
                float val = (val_index < book.quantlist.size() ? static_cast<float>(book.quantlist[val_index]) : 0.0f) 
                            * book.delta_value + book.min_value + last;
                if (book.sequence_p) last = val;
                book.valuelist[i * book.dimensions + d] = val;
                if (book.lookup_values > 0) index_div *= book.lookup_values;
            }
        }
    } else if (book.lookup_type == 2) {
        book.valuelist.assign(book.entries * book.dimensions, 0.0f);
        for (size_t i = 0; i < book.entries; ++i) {
            if (i < book.lengths.size() && book.lengths[i] == 0) continue;
            float last = 0.0f;
            for (size_t d = 0; d < book.dimensions; ++d) {
                uint32_t val_index = static_cast<uint32_t>(i * book.dimensions + d);
                float val = (val_index < book.quantlist.size() ? static_cast<float>(book.quantlist[val_index]) : 0.0f) 
                            * book.delta_value + book.min_value + last;
                if (book.sequence_p) last = val;
                book.valuelist[i * book.dimensions + d] = val;
            }
        }
    }

    return true;
}

bool vorbis_codebook_unpack(VorbisBitReader& reader, VorbisCodebook& book) {
    uint32_t sync = 0;
    if (!reader.read_bits(24, sync) || sync != VORBIS_CODEBOOK_SYNC) {
        return false;
    }

    uint32_t dims = 0, entries = 0;
    if (!reader.read_bits(16, dims) || !reader.read_bits(24, entries)) {
        return false;
    }
    book.dimensions = dims;
    book.entries = entries;
    book.lengths.assign(entries, 0);

    uint32_t ordered = 0;
    if (!reader.read_bits(1, ordered)) return false;

    if (ordered == 0) {
        uint32_t sparse = 0;
        if (!reader.read_bits(1, sparse)) return false;
        for (size_t i = 0; i < entries; ++i) {
            if (sparse) {
                uint32_t flag = 0;
                if (!reader.read_bits(1, flag)) return false;
                if (flag) {
                    uint32_t len = 0;
                    if (!reader.read_bits(5, len)) return false;
                    book.lengths[i] = static_cast<uint8_t>(len + 1);
                } else {
                    book.lengths[i] = 0;
                }
            } else {
                uint32_t len = 0;
                if (!reader.read_bits(5, len)) return false;
                book.lengths[i] = static_cast<uint8_t>(len + 1);
            }
        }
    } else {
        uint32_t current_entry = 0;
        uint32_t current_length = 0;
        if (!reader.read_bits(5, current_length)) return false;
        current_length += 1;

        while (current_entry < entries) {
            uint32_t count = 0;
            uint32_t bits = vorbis_ilog(entries - current_entry);
            if (!reader.read_bits(bits, count)) return false;
            for (uint32_t i = 0; i < count && current_entry < entries; ++i) {
                book.lengths[current_entry++] = static_cast<uint8_t>(current_length);
            }
            current_length++;
        }
    }

    uint32_t lookup_type = 0;
    if (!reader.read_bits(4, lookup_type)) return false;
    book.lookup_type = static_cast<uint8_t>(lookup_type);

    if (lookup_type > 0) {
        uint32_t min_bits = 0, delta_bits = 0, quant_bits = 0, seq_p = 0;
        if (!reader.read_bits(32, min_bits) || !reader.read_bits(32, delta_bits) ||
            !reader.read_bits(4, quant_bits) || !reader.read_bits(1, seq_p)) {
            return false;
        }
        book.min_value = vorbis_unpack_float32(min_bits);
        book.delta_value = vorbis_unpack_float32(delta_bits);
        book.quant_bits = static_cast<uint8_t>(quant_bits + 1);
        book.sequence_p = static_cast<uint8_t>(seq_p);

        if (lookup_type == 1) {
            book.lookup_values = vorbis_book_maptype1_quantvals(entries, dims);
        } else if (lookup_type == 2) {
            book.lookup_values = entries * dims;
        } else {
            return false;
        }

        book.quantlist.assign(book.lookup_values, 0);
        for (size_t i = 0; i < book.lookup_values; ++i) {
            uint32_t val = 0;
            if (!reader.read_bits(book.quant_bits, val)) return false;
            book.quantlist[i] = val;
        }
    }

    return vorbis_codebook_init_tables(book);
}

void vorbis_codebook_pack(VorbisBitWriter& writer, const VorbisCodebook& book) {
    writer.write_bits(VORBIS_CODEBOOK_SYNC, 24);
    writer.write_bits(book.dimensions, 16);
    writer.write_bits(book.entries, 24);

    // Write unordered, non-sparse lengths
    writer.write_bits(0, 1); // ordered = 0
    writer.write_bits(0, 1); // sparse = 0

    for (size_t i = 0; i < book.entries; ++i) {
        uint8_t len = (i < book.lengths.size() && book.lengths[i] > 0) ? book.lengths[i] : 1;
        writer.write_bits(len - 1, 5);
    }

    writer.write_bits(book.lookup_type, 4);
    if (book.lookup_type > 0) {
        writer.write_bits(vorbis_pack_float32(book.min_value), 32);
        writer.write_bits(vorbis_pack_float32(book.delta_value), 32);
        writer.write_bits((book.quant_bits > 0) ? (book.quant_bits - 1) : 0, 4);
        writer.write_bits(book.sequence_p, 1);

        for (size_t i = 0; i < book.lookup_values && i < book.quantlist.size(); ++i) {
            writer.write_bits(book.quantlist[i], book.quant_bits);
        }
    }
}

int vorbis_book_decode(VorbisBitReader& reader, const VorbisCodebook& book) {
    if (book.tree.empty() || book.tree_root < 0) return -1;
    int16_t curr = book.tree_root;
    while (curr >= 0 && book.tree[curr].entry == -1) {
        uint32_t bit = 0;
        if (!reader.read_bits(1, bit)) return -1;
        if (bit == 0) {
            curr = book.tree[curr].left;
        } else {
            curr = book.tree[curr].right;
        }
    }
    return (curr >= 0) ? book.tree[curr].entry : -1;
}

bool vorbis_book_decode_v(VorbisBitReader& reader, const VorbisCodebook& book, float* out_vec) {
    int entry = vorbis_book_decode(reader, book);
    if (entry < 0 || !out_vec) return false;
    if (book.lookup_type > 0 && !book.valuelist.empty()) {
        const float* vals = &book.valuelist[entry * book.dimensions];
        for (size_t d = 0; d < book.dimensions; ++d) {
            out_vec[d] = vals[d];
        }
    } else {
        for (size_t d = 0; d < book.dimensions; ++d) {
            out_vec[d] = static_cast<float>(entry);
        }
    }
    return true;
}

bool vorbis_book_decode_vadd(VorbisBitReader& reader, const VorbisCodebook& book, 
                             float* out_vec, size_t offset, size_t step, size_t count) {
    int entry = vorbis_book_decode(reader, book);
    if (entry < 0 || !out_vec) return false;
    if (book.lookup_type > 0 && !book.valuelist.empty()) {
        const float* vals = &book.valuelist[entry * book.dimensions];
        for (size_t d = 0; d < book.dimensions && (offset + d * step) < count; ++d) {
            out_vec[offset + d * step] += vals[d];
        }
    }
    return true;
}

int vorbis_book_find_best(const VorbisCodebook& book, const float* target_vec) {
    if (book.entries == 0 || book.dimensions == 0 || !target_vec || book.valuelist.empty()) return 0;
    int best_entry = 0;
    float min_dist = 1e30f;

    for (size_t i = 0; i < book.entries; ++i) {
        if (i < book.lengths.size() && book.lengths[i] == 0) continue;
        const float* v = &book.valuelist[i * book.dimensions];
        float dist = 0.0f;
        for (size_t d = 0; d < book.dimensions; ++d) {
            float diff = target_vec[d] - v[d];
            dist += diff * diff;
        }
        if (dist < min_dist) {
            min_dist = dist;
            best_entry = static_cast<int>(i);
        }
    }
    return best_entry;
}

} // namespace audio_codecs::vorbis
