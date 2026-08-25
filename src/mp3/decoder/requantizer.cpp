#include "src/mp3/decoder/requantizer.h"
#include "src/core/math_constants.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::mp3 {

bool Requantizer::decode_scalefactors(core::BitReader& reader, 
                                     const FrameHeader& header, 
                                     const SideInfo& side, 
                                     int gr, int ch, 
                                     ScalefactorData& sf,
                                     size_t& part2_bits_read) {
    size_t start_pos = reader.get_position_bits();
    const GranuleChannelInfo& gi = side.gr[gr][ch];

    if (header.version == MpegVersion::Mpeg1) {
        uint8_t slen1 = SLEN1_MPEG1[gi.scalefac_compress];
        uint8_t slen2 = SLEN2_MPEG1[gi.scalefac_compress];

        if (gi.window_switching_flag && (gi.block_type == 2)) {
            if (gi.mixed_block_flag) {
                for (int s = 0; s < 8; ++s) {
                    sf.l[s] = (slen1 > 0) ? static_cast<int16_t>(reader.read_bits(slen1)) : 0;
                }
                for (int s = 3; s < 6; ++s) {
                    for (int w = 0; w < 3; ++w) {
                        sf.s[s][w] = (slen1 > 0) ? static_cast<int16_t>(reader.read_bits(slen1)) : 0;
                    }
                }
                for (int s = 6; s < 12; ++s) {
                    for (int w = 0; w < 3; ++w) {
                        sf.s[s][w] = (slen2 > 0) ? static_cast<int16_t>(reader.read_bits(slen2)) : 0;
                    }
                }
            } else {
                for (int s = 0; s < 6; ++s) {
                    for (int w = 0; w < 3; ++w) {
                        sf.s[s][w] = (slen1 > 0) ? static_cast<int16_t>(reader.read_bits(slen1)) : 0;
                    }
                }
                for (int s = 6; s < 12; ++s) {
                    for (int w = 0; w < 3; ++w) {
                        sf.s[s][w] = (slen2 > 0) ? static_cast<int16_t>(reader.read_bits(slen2)) : 0;
                    }
                }
            }
        } else {
            // Long blocks
            // scfsi bands: 0: 0..5, 1: 6..10, 2: 11..15, 3: 16..20
            static const int SCFSI_RANGES[4][2] = {
                {0, 6}, {6, 11}, {11, 16}, {16, 21}
            };

            for (int scf_band = 0; scf_band < 4; ++scf_band) {
                int slen = (scf_band < 2) ? slen1 : slen2;
                int start_sfb = SCFSI_RANGES[scf_band][0];
                int end_sfb   = SCFSI_RANGES[scf_band][1];

                if (gr == 0 || side.scfsi[ch][scf_band] == 0) {
                    for (int s = start_sfb; s < end_sfb; ++s) {
                        sf.l[s] = (slen > 0) ? static_cast<int16_t>(reader.read_bits(slen)) : 0;
                    }
                }
                // If gr == 1 and scfsi == 1, sf.l[s] remains from gr 0
            }
            sf.l[21] = 0;
            sf.l[22] = 0;
        }
    } else {
        // MPEG-2 / 2.5 scalefactor decoding
        // Simple 4-partition unpack
        for (int s = 0; s < 21; ++s) {
            sf.l[s] = static_cast<int16_t>(reader.read_bits(4));
        }
    }

    part2_bits_read = reader.get_position_bits() - start_pos;
    return true;
}

void Requantizer::requantize_granule(const int16_t* is, 
                                     const ScalefactorData& sf, 
                                     const GranuleChannelInfo& gi, 
                                     const FrameHeader& header, 
                                     float* xr_576) {
    if (!is || !xr_576) return;

    float global_scale = std::pow(2.0f, 0.25f * (static_cast<float>(gi.global_gain) - 210.0f));
    float sf_mult = gi.scalefac_scale ? 1.0f : 0.5f;

    if (gi.window_switching_flag && (gi.block_type == 2)) {
        // Short blocks
        const uint16_t* sfb_table = get_scalefac_band_table_short(header.sample_rate);
        
        float subblock_scales[3];
        for (int w = 0; w < 3; ++w) {
            subblock_scales[w] = std::pow(2.0f, 0.25f * (-8.0f * static_cast<float>(gi.subblock_gain[w])));
        }

        int line = 0;
        for (int s = 0; s < 12; ++s) {
            int width = sfb_table[s + 1] - sfb_table[s];
            for (int w = 0; w < 3; ++w) {
                float sf_factor = std::pow(2.0f, -sf_mult * static_cast<float>(sf.s[s][w]));
                float total_scale = global_scale * subblock_scales[w] * sf_factor;

                for (int k = 0; k < width; ++k) {
                    if (line >= 576) break;
                    int16_t val = is[line];
                    if (val == 0) {
                        xr_576[line] = 0.0f;
                    } else {
                        float abs_val = static_cast<float>(std::abs(val));
                        float is_pow = std::pow(abs_val, 4.0f / 3.0f);
                        xr_576[line] = (val < 0) ? (-is_pow * total_scale) : (is_pow * total_scale);
                    }
                    line++;
                }
            }
        }
        for (; line < 576; ++line) {
            xr_576[line] = 0.0f;
        }
    } else {
        // Long blocks
        const uint16_t* sfb_table = get_scalefac_band_table_long(header.sample_rate);
        int line = 0;

        for (int s = 0; s < 22; ++s) {
            int start_idx = sfb_table[s];
            int end_idx   = sfb_table[s + 1];
            
            float pretab_val = (gi.preflag) ? static_cast<float>(PRETAB[s]) : 0.0f;
            float sf_factor = std::pow(2.0f, -sf_mult * (static_cast<float>(sf.l[s]) + pretab_val));
            float total_scale = global_scale * sf_factor;

            for (line = start_idx; line < end_idx && line < 576; ++line) {
                int16_t val = is[line];
                if (val == 0) {
                    xr_576[line] = 0.0f;
                } else {
                    float abs_val = static_cast<float>(std::abs(val));
                    float is_pow = std::pow(abs_val, 4.0f / 3.0f);
                    xr_576[line] = (val < 0) ? (-is_pow * total_scale) : (is_pow * total_scale);
                }
            }
        }
        for (; line < 576; ++line) {
            xr_576[line] = 0.0f;
        }
    }
}

void Requantizer::reorder_short_blocks(float* xr_576, const FrameHeader& header) {
    if (!xr_576) return;
    const uint16_t* sfb_table = get_scalefac_band_table_short(header.sample_rate);

    float temp[576];
    std::memcpy(temp, xr_576, sizeof(temp));

    int src = 0;
    for (int sb = 0; sb < 32; ++sb) {
        for (int w = 0; w < 3; ++w) {
            for (int k = 0; k < 6; ++k) {
                int dst = sb * 18 + w * 6 + k;
                if (dst < 576 && src < 576) {
                    xr_576[dst] = temp[src++];
                }
            }
        }
    }
}

void Requantizer::process_stereo(float* xr_left, float* xr_right, 
                                 const GranuleChannelInfo& gi_left, 
                                 const GranuleChannelInfo& gi_right, 
                                 const FrameHeader& header) {
    if (!xr_left || !xr_right || header.channels != 2) return;

    if (header.mode == MpegMode::JointStereo) {
        if (header.ms_stereo && !header.intensity_stereo) {
            // Pure MS Stereo
            for (int i = 0; i < 576; ++i) {
                float m = xr_left[i];
                float s = xr_right[i];
                xr_left[i]  = (m + s) * constants::INV_SQRT2;
                xr_right[i] = (m - s) * constants::INV_SQRT2;
            }
        } else if (header.ms_stereo && header.intensity_stereo) {
            // Mixed MS & Intensity stereo
            for (int i = 0; i < 576; ++i) {
                float m = xr_left[i];
                float s = xr_right[i];
                xr_left[i]  = (m + s) * constants::INV_SQRT2;
                xr_right[i] = (m - s) * constants::INV_SQRT2;
            }
        }
    }
}

void Requantizer::alias_reduction(float* xr_576, const GranuleChannelInfo& gi) {
    if (!xr_576) return;
    if (gi.window_switching_flag && (gi.block_type == 2) && !gi.mixed_block_flag) {
        // Short blocks do not use alias reduction
        return;
    }

    int num_subbands = (gi.window_switching_flag && gi.mixed_block_flag) ? 2 : 31;

    for (int sb = 1; sb <= num_subbands; ++sb) {
        for (int i = 0; i < 8; ++i) {
            int idx1 = 18 * sb - 1 - i;
            int idx2 = 18 * sb + i;

            if (idx1 >= 0 && idx2 < 576) {
                float x1 = xr_576[idx1];
                float x2 = xr_576[idx2];

                xr_576[idx1] = x1 * ALIAS_CS[i] - x2 * ALIAS_CA[i];
                xr_576[idx2] = x2 * ALIAS_CS[i] + x1 * ALIAS_CA[i];
            }
        }
    }
}

} // namespace audio_codecs::mp3
