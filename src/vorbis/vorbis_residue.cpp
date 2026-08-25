#include "src/vorbis/vorbis_residue.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::vorbis {

bool vorbis_residue_unpack(VorbisBitReader& reader, VorbisResidueConfig& cfg) {
    uint32_t type = 0, begin = 0, end = 0, part_size = 0, classes = 0, classbook = 0;
    if (!reader.read_bits(16, type) || !reader.read_bits(24, begin) || !reader.read_bits(24, end) ||
        !reader.read_bits(24, part_size) || !reader.read_bits(6, classes) || !reader.read_bits(8, classbook)) {
        return false;
    }

    cfg.type = static_cast<uint8_t>(type);
    cfg.begin = begin;
    cfg.end = end;
    cfg.partition_size = part_size + 1;
    cfg.classifications = static_cast<uint8_t>(classes + 1);
    cfg.classbook = static_cast<uint8_t>(classbook);

    // Read cascade bitmaps
    uint8_t cascade[VORBIS_MAX_RESIDUE_CLASSIFICATIONS]{0};
    for (size_t j = 0; j < cfg.classifications && j < VORBIS_MAX_RESIDUE_CLASSIFICATIONS; ++j) {
        uint32_t low_bits = 0, bitflag = 0;
        if (!reader.read_bits(3, low_bits)) return false;
        if (!reader.read_bits(1, bitflag)) return false;
        uint32_t high_bits = 0;
        if (bitflag) {
            if (!reader.read_bits(5, high_bits)) return false;
        }
        cascade[j] = static_cast<uint8_t>((high_bits << 3) | low_bits);
    }

    // Read book numbers for active cascade bits
    for (size_t j = 0; j < cfg.classifications && j < VORBIS_MAX_RESIDUE_CLASSIFICATIONS; ++j) {
        for (size_t k = 0; k < 8; ++k) {
            if (cascade[j] & (1 << k)) {
                uint32_t book = 0;
                if (!reader.read_bits(8, book)) return false;
                cfg.books[j][k] = static_cast<int16_t>(book);
            } else {
                cfg.books[j][k] = -1;
            }
        }
    }

    return true;
}

void vorbis_residue_pack(VorbisBitWriter& writer, const VorbisResidueConfig& cfg) {
    writer.write_bits(cfg.type, 16);
    writer.write_bits(cfg.begin, 24);
    writer.write_bits(cfg.end, 24);
    writer.write_bits(cfg.partition_size - 1, 24);
    writer.write_bits(cfg.classifications - 1, 6);
    writer.write_bits(cfg.classbook, 8);

    for (size_t j = 0; j < cfg.classifications; ++j) {
        uint8_t casc = 0;
        for (size_t k = 0; k < 8; ++k) {
            if (cfg.books[j][k] >= 0) casc |= (1 << k);
        }
        uint32_t low = casc & 0x07;
        uint32_t high = (casc >> 3) & 0x1F;
        writer.write_bits(low, 3);
        if (high > 0) {
            writer.write_bits(1, 1);
            writer.write_bits(high, 5);
        } else {
            writer.write_bits(0, 1);
        }
    }

    for (size_t j = 0; j < cfg.classifications; ++j) {
        for (size_t k = 0; k < 8; ++k) {
            if (cfg.books[j][k] >= 0) {
                writer.write_bits(cfg.books[j][k], 8);
            }
        }
    }
}

bool vorbis_residue_decode(VorbisBitReader& reader, const VorbisResidueConfig& cfg,
                           const VorbisCodebook* books, size_t book_count,
                           float** ch_residues, const bool* ch_nonzero, 
                           uint8_t ch_count, size_t n2) {
    if (!ch_residues || ch_count == 0 || n2 == 0) return false;

    // Check if at least one channel has nonzero floor
    bool any_active = false;
    for (uint8_t c = 0; c < ch_count; ++c) {
        if (!ch_nonzero || ch_nonzero[c]) {
            any_active = true;
            break;
        }
    }
    if (!any_active) return true;

    size_t actual_end = std::min<size_t>(cfg.end, n2);
    size_t actual_begin = std::min<size_t>(cfg.begin, actual_end);
    size_t total_samples = actual_end - actual_begin;
    if (total_samples == 0 || cfg.partition_size == 0) return true;

    size_t partitions_per_word = (cfg.classbook < book_count) ? books[cfg.classbook].dimensions : 1;
    if (partitions_per_word == 0) partitions_per_word = 1;

    size_t partition_count = total_samples / cfg.partition_size;

    // Decode Residue Type 2 (interleaved stereo) or Type 0/1
    if (cfg.type == 2) {
        // Interleaved buffer of size total_samples * ch_count
        alignas(16) static float interleaved[VORBIS_MAX_CHANNELS * VORBIS_MAX_BLOCK_SIZE / 2];
        size_t total_interleaved = total_samples * ch_count;
        std::memset(interleaved, 0, total_interleaved * sizeof(float));

        size_t part_count = total_interleaved / cfg.partition_size;
        std::vector<uint8_t> part_classes(part_count, 0);

        // Stage 0: classification
        for (size_t p = 0; p < part_count; p += partitions_per_word) {
            int cval = (cfg.classbook < book_count) ? vorbis_book_decode(reader, books[cfg.classbook]) : 0;
            if (cval < 0) cval = 0;
            for (int i = static_cast<int>(partitions_per_word) - 1; i >= 0; --i) {
                if (p + i < part_count) {
                    part_classes[p + i] = static_cast<uint8_t>(cval % cfg.classifications);
                }
                cval /= cfg.classifications;
            }
        }

        // Multi-stage residue vector decoding
        for (size_t s = 0; s < 8; ++s) {
            for (size_t p = 0; p < part_count; ++p) {
                uint8_t pclass = part_classes[p];
                int16_t book_idx = cfg.books[pclass][s];
                if (book_idx >= 0 && static_cast<size_t>(book_idx) < book_count) {
                    const VorbisCodebook& book = books[book_idx];
                    size_t offset = p * cfg.partition_size;
                    for (size_t j = 0; j < cfg.partition_size; j += book.dimensions) {
                        vorbis_book_decode_vadd(reader, book, interleaved, offset + j, 1, total_interleaved);
                    }
                }
            }
        }

        // De-interleave into channels
        for (size_t i = 0; i < total_samples; ++i) {
            for (uint8_t c = 0; c < ch_count; ++c) {
                ch_residues[c][actual_begin + i] += interleaved[i * ch_count + c];
            }
        }
    } else {
        // Residue Type 0 or 1 (per channel)
        for (uint8_t c = 0; c < ch_count; ++c) {
            if (ch_nonzero && !ch_nonzero[c]) continue;
            std::vector<uint8_t> part_classes(partition_count, 0);

            for (size_t p = 0; p < partition_count; p += partitions_per_word) {
                int cval = (cfg.classbook < book_count) ? vorbis_book_decode(reader, books[cfg.classbook]) : 0;
                if (cval < 0) cval = 0;
                for (int i = static_cast<int>(partitions_per_word) - 1; i >= 0; --i) {
                    if (p + i < partition_count) {
                        part_classes[p + i] = static_cast<uint8_t>(cval % cfg.classifications);
                    }
                    cval /= cfg.classifications;
                }
            }

            for (size_t s = 0; s < 8; ++s) {
                for (size_t p = 0; p < partition_count; ++p) {
                    uint8_t pclass = part_classes[p];
                    int16_t book_idx = cfg.books[pclass][s];
                    if (book_idx >= 0 && static_cast<size_t>(book_idx) < book_count) {
                        const VorbisCodebook& book = books[book_idx];
                        size_t offset = actual_begin + p * cfg.partition_size;
                        for (size_t j = 0; j < cfg.partition_size; j += book.dimensions) {
                            vorbis_book_decode_vadd(reader, book, ch_residues[c], offset + j, 1, n2);
                        }
                    }
                }
            }
        }
    }

    return true;
}

void vorbis_residue_encode(VorbisBitWriter& writer, const VorbisResidueConfig& cfg,
                           const VorbisCodebook* books, size_t book_count,
                           float** ch_residues, const bool* ch_nonzero,
                           uint8_t ch_count, size_t n2) {
    if (!ch_residues || ch_count == 0 || n2 == 0) return;

    size_t actual_end = std::min<size_t>(cfg.end, n2);
    size_t actual_begin = std::min<size_t>(cfg.begin, actual_end);
    size_t total_samples = actual_end - actual_begin;
    if (total_samples == 0 || cfg.partition_size == 0) return;

    size_t partitions_per_word = (cfg.classbook < book_count) ? books[cfg.classbook].dimensions : 1;
    if (partitions_per_word == 0) partitions_per_word = 1;

    if (cfg.type == 2) {
        size_t total_interleaved = total_samples * ch_count;
        alignas(16) static float interleaved[VORBIS_MAX_CHANNELS * VORBIS_MAX_BLOCK_SIZE / 2];
        for (size_t i = 0; i < total_samples; ++i) {
            for (uint8_t c = 0; c < ch_count; ++c) {
                interleaved[i * ch_count + c] = ch_residues[c][actual_begin + i];
            }
        }

        size_t part_count = total_interleaved / cfg.partition_size;

        // Stage 0: classification codebook (all zero class for standard profile)
        for (size_t p = 0; p < part_count; p += partitions_per_word) {
            if (cfg.classbook < book_count) {
                writer.write_bits(books[cfg.classbook].codewords[0], books[cfg.classbook].lengths[0]);
            }
        }

        // Quantize and write residue codewords using stage 0 book
        int16_t book_idx = cfg.books[0][0];
        if (book_idx >= 0 && static_cast<size_t>(book_idx) < book_count) {
            const VorbisCodebook& book = books[book_idx];
            for (size_t i = 0; i < total_interleaved; i += book.dimensions) {
                int best_entry = vorbis_book_find_best(book, &interleaved[i]);
                if (best_entry >= 0 && static_cast<size_t>(best_entry) < book.entries) {
                    writer.write_bits(book.codewords[best_entry], book.lengths[best_entry]);
                }
            }
        }
    }
}

} // namespace audio_codecs::vorbis
