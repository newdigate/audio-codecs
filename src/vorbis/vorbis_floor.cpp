#include "src/vorbis/vorbis_floor.h"
#include "src/vorbis/vorbis_common.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace audio_codecs::vorbis {

void vorbis_floor1_setup_neighbors(VorbisFloor1Config& cfg) {
    size_t count = cfg.post_list.size();
    if (count < 2) return;

    // Build sorted indices
    cfg.sorted_indices.resize(count);
    std::iota(cfg.sorted_indices.begin(), cfg.sorted_indices.end(), 0);
    std::sort(cfg.sorted_indices.begin(), cfg.sorted_indices.end(), 
              [&cfg](uint32_t a, uint32_t b) {
                  return cfg.post_list[a] < cfg.post_list[b];
              });

    cfg.low_neighbors.assign(count, 0);
    cfg.high_neighbors.assign(count, 1);

    for (size_t i = 2; i < count; ++i) {
        uint32_t xi = cfg.post_list[i];
        uint32_t best_low = 0;
        uint32_t best_low_x = 0;
        uint32_t best_high = 1;
        uint32_t best_high_x = 0xFFFFFFFFU;

        for (size_t j = 0; j < i; ++j) {
            uint32_t xj = cfg.post_list[j];
            if (xj < xi && (xj >= best_low_x || best_low_x == 0)) {
                best_low_x = xj;
                best_low = static_cast<uint32_t>(j);
            }
            if (xj > xi && xj <= best_high_x) {
                best_high_x = xj;
                best_high = static_cast<uint32_t>(j);
            }
        }
        cfg.low_neighbors[i] = best_low;
        cfg.high_neighbors[i] = best_high;
    }
}

bool vorbis_floor1_unpack(VorbisBitReader& reader, VorbisFloor1Config& cfg) {
    uint32_t partitions = 0;
    if (!reader.read_bits(5, partitions)) return false;
    cfg.partitions = static_cast<uint8_t>(partitions);

    uint8_t max_class = 0;
    for (size_t i = 0; i < cfg.partitions && i < VORBIS_MAX_FLOOR1_PARTITIONS; ++i) {
        uint32_t pclass = 0;
        if (!reader.read_bits(4, pclass)) return false;
        cfg.partition_class[i] = static_cast<uint8_t>(pclass);
        if (cfg.partition_class[i] > max_class) {
            max_class = cfg.partition_class[i];
        }
    }

    size_t class_count = max_class + 1;
    for (size_t i = 0; i < class_count && i < 16; ++i) {
        uint32_t dim = 0, subclasses = 0;
        if (!reader.read_bits(3, dim) || !reader.read_bits(2, subclasses)) return false;
        cfg.class_dimensions[i] = static_cast<uint8_t>(dim + 1);
        cfg.class_subclasses[i] = static_cast<uint8_t>(subclasses);

        if (subclasses > 0) {
            uint32_t masterbook = 0;
            if (!reader.read_bits(8, masterbook)) return false;
            cfg.class_masterbooks[i] = static_cast<uint8_t>(masterbook);
        }

        size_t sub_count = (1U << subclasses);
        for (size_t j = 0; j < sub_count && j < 8; ++j) {
            uint32_t book = 0;
            if (!reader.read_bits(8, book)) return false;
            cfg.subclass_books[i][j] = static_cast<int16_t>(book) - 1;
        }
    }

    uint32_t mult = 0, rangebits = 0;
    if (!reader.read_bits(2, mult) || !reader.read_bits(4, rangebits)) return false;
    cfg.multiplier = static_cast<uint8_t>(mult + 1);
    cfg.rangebits = static_cast<uint8_t>(rangebits);

    cfg.post_list.clear();
    cfg.post_list.push_back(0);
    cfg.post_list.push_back(1U << cfg.rangebits);

    for (size_t i = 0; i < cfg.partitions; ++i) {
        uint8_t c = cfg.partition_class[i];
        uint8_t dim = cfg.class_dimensions[c];
        for (size_t j = 0; j < dim; ++j) {
            uint32_t x = 0;
            if (!reader.read_bits(cfg.rangebits, x)) return false;
            cfg.post_list.push_back(x);
        }
    }

    vorbis_floor1_setup_neighbors(cfg);
    return true;
}

void vorbis_floor1_pack(VorbisBitWriter& writer, const VorbisFloor1Config& cfg) {
    writer.write_bits(cfg.partitions, 5);

    uint8_t max_class = 0;
    for (size_t i = 0; i < cfg.partitions; ++i) {
        writer.write_bits(cfg.partition_class[i], 4);
        if (cfg.partition_class[i] > max_class) max_class = cfg.partition_class[i];
    }

    size_t class_count = max_class + 1;
    for (size_t i = 0; i < class_count; ++i) {
        writer.write_bits(cfg.class_dimensions[i] - 1, 3);
        writer.write_bits(cfg.class_subclasses[i], 2);
        if (cfg.class_subclasses[i] > 0) {
            writer.write_bits(cfg.class_masterbooks[i], 8);
        }
        size_t sub_count = (1U << cfg.class_subclasses[i]);
        for (size_t j = 0; j < sub_count; ++j) {
            writer.write_bits(cfg.subclass_books[i][j] + 1, 8);
        }
    }

    writer.write_bits(cfg.multiplier - 1, 2);
    writer.write_bits(cfg.rangebits, 4);

    for (size_t i = 2; i < cfg.post_list.size(); ++i) {
        writer.write_bits(cfg.post_list[i], cfg.rangebits);
    }
}

bool vorbis_floor1_decode(VorbisBitReader& reader, const VorbisFloor1Config& cfg, 
                          const VorbisCodebook* books, size_t book_count, 
                          int32_t* out_y) {
    if (!out_y) return false;
    uint32_t nonzero = 0;
    if (!reader.read_bits(1, nonzero) || nonzero == 0) {
        return false;
    }

    int32_t range = (cfg.multiplier == 1) ? 256 : ((cfg.multiplier == 2) ? 128 : ((cfg.multiplier == 3) ? 86 : 64));
    uint32_t y_bits = vorbis_ilog(range - 1);

    uint32_t y0 = 0, y1 = 0;
    if (!reader.read_bits(y_bits, y0) || !reader.read_bits(y_bits, y1)) return false;
    out_y[0] = static_cast<int32_t>(y0);
    out_y[1] = static_cast<int32_t>(y1);

    size_t post_idx = 2;
    for (size_t p = 0; p < cfg.partitions; ++p) {
        uint8_t c = cfg.partition_class[p];
        uint8_t dim = cfg.class_dimensions[c];
        uint8_t subclasses = cfg.class_subclasses[c];

        if (subclasses == 0) {
            for (size_t j = 0; j < dim && post_idx < cfg.post_list.size(); ++j) {
                out_y[post_idx++] = 0;
            }
        } else {
            uint8_t mbook = cfg.class_masterbooks[c];
            int cval = (mbook < book_count) ? vorbis_book_decode(reader, books[mbook]) : 0;
            if (cval < 0) cval = 0;

            for (size_t j = 0; j < dim && post_idx < cfg.post_list.size(); ++j) {
                int csub = cval & ((1 << subclasses) - 1);
                cval >>= subclasses;
                int16_t sbook = cfg.subclass_books[c][csub];
                if (sbook >= 0 && static_cast<size_t>(sbook) < book_count) {
                    int val = vorbis_book_decode(reader, books[sbook]);
                    out_y[post_idx++] = (val >= 0) ? val : 0;
                } else {
                    out_y[post_idx++] = 0;
                }
            }
        }
    }

    return true;
}

void vorbis_floor1_render(const VorbisFloor1Config& cfg, const int32_t* y_vals, 
                          float* out_floor, size_t n2) {
    if (!y_vals || !out_floor || n2 == 0 || cfg.post_list.size() < 2) return;

    size_t post_count = cfg.post_list.size();
    int32_t final_y[VORBIS_MAX_FLOOR1_POSTS]{0};
    final_y[0] = y_vals[0];
    final_y[1] = y_vals[1];
    int32_t range = (cfg.multiplier == 1) ? 256 : ((cfg.multiplier == 2) ? 128 : ((cfg.multiplier == 3) ? 86 : 64));

    for (size_t i = 2; i < post_count && i < VORBIS_MAX_FLOOR1_POSTS; ++i) {
        uint32_t low = cfg.low_neighbors[i];
        uint32_t high = cfg.high_neighbors[i];
        int32_t pred = vorbis_render_point(cfg.post_list[low], final_y[low],
                                           cfg.post_list[high], final_y[high],
                                           cfg.post_list[i]);
        int32_t val = y_vals[i];
        int32_t highroom = range - pred;
        int32_t lowroom = pred;
        int32_t room = 2 * std::min(highroom, lowroom);
        if (val >= room) {
            if (highroom > lowroom) {
                final_y[i] = val - lowroom + pred;
            } else {
                final_y[i] = pred - (val - highroom);
            }
        } else if (val & 1) {
            final_y[i] = pred - ((val + 1) / 2);
        } else {
            final_y[i] = pred + (val / 2);
        }
        final_y[i] = std::max<int32_t>(0, std::min<int32_t>(range - 1, final_y[i]));
    }

    // Line interpolation across sorted post list
    for (size_t i = 0; i + 1 < cfg.sorted_indices.size(); ++i) {
        uint32_t idx_a = cfg.sorted_indices[i];
        uint32_t idx_b = cfg.sorted_indices[i + 1];
        int32_t xa = cfg.post_list[idx_a];
        int32_t xb = cfg.post_list[idx_b];
        int32_t ya = final_y[idx_a] * cfg.multiplier;
        int32_t yb = final_y[idx_b] * cfg.multiplier;

        int32_t x_start = std::max<int32_t>(0, xa);
        int32_t x_end = std::min<int32_t>(static_cast<int32_t>(n2), xb);

        for (int32_t x = x_start; x < x_end; ++x) {
            int32_t y = vorbis_render_point(xa, ya, xb, yb, x);
            int32_t y_clamped = std::max<int32_t>(0, std::min<int32_t>(255, y));
            out_floor[x] = static_cast<float>(std::pow(10.0, 7.0 * (static_cast<double>(y_clamped) / 255.0 - 1.0)));
        }
    }
}

void vorbis_floor1_fit(const float* target_spectrum, size_t n2, 
                       const VorbisFloor1Config& cfg, int32_t* out_y) {
    if (!target_spectrum || !out_y || n2 == 0 || cfg.post_list.empty()) return;
    int32_t range = (cfg.multiplier == 1) ? 256 : ((cfg.multiplier == 2) ? 128 : ((cfg.multiplier == 3) ? 86 : 64));

    for (size_t i = 0; i < cfg.post_list.size(); ++i) {
        size_t x = std::min<size_t>(cfg.post_list[i], n2 - 1);
        size_t win_start = (x > 4) ? (x - 4) : 0;
        size_t win_end = std::min<size_t>(x + 4, n2);

        float energy = 0.0f;
        for (size_t k = win_start; k < win_end; ++k) {
            energy += target_spectrum[k] * target_spectrum[k];
        }
        energy /= static_cast<float>(win_end - win_start);
        float rms = std::sqrt(std::max(1e-12f, energy));

        // Map rms (e.g. 1e-7 to 1.0) to floor Y range
        float db = 20.0f * std::log10(rms); // e.g. -140 to 0 dB
        float y_norm = (db + 140.0f) / 140.0f;
        int32_t y_int = static_cast<int32_t>(std::round(y_norm * (range - 1)));
        out_y[i] = std::max<int32_t>(1, std::min<int32_t>(range - 1, y_int));
    }
}

void vorbis_floor1_encode(VorbisBitWriter& writer, const VorbisFloor1Config& cfg,
                          const VorbisCodebook* books, size_t book_count,
                          const int32_t* y_vals) {
    if (!y_vals) return;
    writer.write_bits(1, 1); // nonzero = 1

    int32_t range = (cfg.multiplier == 1) ? 256 : ((cfg.multiplier == 2) ? 128 : ((cfg.multiplier == 3) ? 86 : 64));
    uint32_t y_bits = vorbis_ilog(range - 1);

    writer.write_bits(y_vals[0], y_bits);
    writer.write_bits(y_vals[1], y_bits);

    size_t post_idx = 2;
    for (size_t p = 0; p < cfg.partitions; ++p) {
        uint8_t c = cfg.partition_class[p];
        uint8_t dim = cfg.class_dimensions[c];
        uint8_t subclasses = cfg.class_subclasses[c];

        if (subclasses == 0) {
            post_idx += dim;
        } else {
            uint8_t mbook = cfg.class_masterbooks[c];
            if (mbook < book_count) {
                // Encode subclass index (0 for simplified codebooks)
                writer.write_bits(0, books[mbook].lengths[0]);
            }
            for (size_t j = 0; j < dim && post_idx < cfg.post_list.size(); ++j) {
                int16_t sbook = cfg.subclass_books[c][0];
                if (sbook >= 0 && static_cast<size_t>(sbook) < book_count) {
                    uint32_t val = static_cast<uint32_t>(y_vals[post_idx]);
                    if (val < books[sbook].entries) {
                        writer.write_bits(books[sbook].codewords[val], books[sbook].lengths[val]);
                    } else {
                        writer.write_bits(0, books[sbook].lengths[0]);
                    }
                }
                post_idx++;
            }
        }
    }
}

} // namespace audio_codecs::vorbis
