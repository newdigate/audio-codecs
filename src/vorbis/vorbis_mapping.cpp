#include "src/vorbis/vorbis_mapping.h"
#include "src/vorbis/vorbis_common.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::vorbis {

bool vorbis_mapping_unpack(VorbisBitReader& reader, VorbisMappingConfig& cfg, uint8_t channels) {
    uint32_t submaps = 0;
    uint32_t submap_flag = 0;
    if (!reader.read_bits(1, submap_flag)) return false;

    if (submap_flag) {
        if (!reader.read_bits(4, submaps)) return false;
        cfg.submaps = static_cast<uint8_t>(submaps + 1);
    } else {
        cfg.submaps = 1;
    }

    uint32_t coupling_flag = 0;
    if (!reader.read_bits(1, coupling_flag)) return false;

    if (coupling_flag) {
        uint32_t steps = 0;
        if (!reader.read_bits(8, steps)) return false;
        cfg.coupling_steps = static_cast<uint8_t>(steps + 1);

        uint32_t ch_bits = vorbis_ilog(channels - 1);
        for (size_t i = 0; i < cfg.coupling_steps && i < VORBIS_MAX_COUPLING_STEPS; ++i) {
            uint32_t mag = 0, angle = 0;
            if (!reader.read_bits(ch_bits, mag) || !reader.read_bits(ch_bits, angle)) return false;
            cfg.coupling_mag[i] = static_cast<uint8_t>(mag);
            cfg.coupling_angle[i] = static_cast<uint8_t>(angle);
        }
    } else {
        cfg.coupling_steps = 0;
    }

    uint32_t reserved = 0;
    if (!reader.read_bits(2, reserved) || reserved != 0) return false;

    if (cfg.submaps > 1) {
        for (size_t c = 0; c < channels && c < VORBIS_MAX_CHANNELS; ++c) {
            uint32_t mux = 0;
            if (!reader.read_bits(4, mux)) return false;
            cfg.ch_mux[c] = static_cast<uint8_t>(mux);
        }
    } else {
        for (size_t c = 0; c < channels && c < VORBIS_MAX_CHANNELS; ++c) {
            cfg.ch_mux[c] = 0;
        }
    }

    for (size_t s = 0; s < cfg.submaps && s < VORBIS_MAX_SUBMAPS; ++s) {
        uint32_t time_unused = 0, floor_idx = 0, res_idx = 0;
        if (!reader.read_bits(8, time_unused) || !reader.read_bits(8, floor_idx) || !reader.read_bits(8, res_idx)) {
            return false;
        }
        cfg.submap_floor[s] = static_cast<uint8_t>(floor_idx);
        cfg.submap_residue[s] = static_cast<uint8_t>(res_idx);
    }

    return true;
}

void vorbis_mapping_pack(VorbisBitWriter& writer, const VorbisMappingConfig& cfg, uint8_t channels) {
    if (cfg.submaps > 1) {
        writer.write_bits(1, 1);
        writer.write_bits(cfg.submaps - 1, 4);
    } else {
        writer.write_bits(0, 1);
    }

    if (cfg.coupling_steps > 0) {
        writer.write_bits(1, 1);
        writer.write_bits(cfg.coupling_steps - 1, 8);
        uint32_t ch_bits = vorbis_ilog(channels - 1);
        for (size_t i = 0; i < cfg.coupling_steps; ++i) {
            writer.write_bits(cfg.coupling_mag[i], ch_bits);
            writer.write_bits(cfg.coupling_angle[i], ch_bits);
        }
    } else {
        writer.write_bits(0, 1);
    }

    writer.write_bits(0, 2); // Reserved 2 zero bits

    if (cfg.submaps > 1) {
        for (size_t c = 0; c < channels; ++c) {
            writer.write_bits(cfg.ch_mux[c], 4);
        }
    }

    for (size_t s = 0; s < cfg.submaps; ++s) {
        writer.write_bits(0, 8); // time placeholder
        writer.write_bits(cfg.submap_floor[s], 8);
        writer.write_bits(cfg.submap_residue[s], 8);
    }
}

void vorbis_mapping_decouple(const VorbisMappingConfig& cfg, float** ch_spectra, size_t n2) {
    if (!ch_spectra || n2 == 0) return;

    for (size_t i = 0; i < cfg.coupling_steps; ++i) {
        uint8_t mag_ch = cfg.coupling_mag[i];
        uint8_t ang_ch = cfg.coupling_angle[i];
        float* mag = ch_spectra[mag_ch];
        float* ang = ch_spectra[ang_ch];
        if (!mag || !ang) continue;

        for (size_t k = 0; k < n2; ++k) {
            float m = mag[k];
            float a = ang[k];
            float l = 0.0f, r = 0.0f;

            if (m > 0.0f) {
                if (a > 0.0f) {
                    l = m;
                    r = m - a;
                } else {
                    r = m;
                    l = m + a;
                }
            } else {
                if (a > 0.0f) {
                    l = m;
                    r = m + a;
                } else {
                    r = m;
                    l = m - a;
                }
            }
            mag[k] = l;
            ang[k] = r;
        }
    }
}

void vorbis_mapping_couple(const VorbisMappingConfig& cfg, float** ch_spectra, size_t n2) {
    if (!ch_spectra || n2 == 0) return;

    for (size_t i = 0; i < cfg.coupling_steps; ++i) {
        uint8_t mag_ch = cfg.coupling_mag[i];
        uint8_t ang_ch = cfg.coupling_angle[i];
        float* left = ch_spectra[mag_ch];
        float* right = ch_spectra[ang_ch];
        if (!left || !right) continue;

        for (size_t k = 0; k < n2; ++k) {
            float l = left[k];
            float r = right[k];
            float mag = 0.0f, ang = 0.0f;

            if (std::fabs(l) >= std::fabs(r)) {
                mag = l;
                ang = (l > 0.0f) ? (l - r) : (r - l);
            } else {
                mag = r;
                ang = (r > 0.0f) ? (l - r) : (r - l);
            }
            left[k] = mag;
            right[k] = ang;
        }
    }
}

} // namespace audio_codecs::vorbis
