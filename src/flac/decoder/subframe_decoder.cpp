#include "src/flac/decoder/subframe_decoder.h"
#include "src/flac/decoder/rice_decoder.h"
#include "src/flac/encoder/fixed_predictor.h"

namespace audio_codecs::flac {

static inline int32_t sign_extend(uint32_t val, uint8_t bits) {
    if (bits == 0 || bits >= 32) return static_cast<int32_t>(val);
    if (val & (1u << (bits - 1))) {
        return static_cast<int32_t>(val | (~0u << bits));
    }
    return static_cast<int32_t>(val);
}

void LpcPredictor::restore_samples(const int32_t* residual, 
                                  size_t count, 
                                  int order, 
                                  const int32_t* qlp_coeff, 
                                  int qlp_shift, 
                                  int32_t* inout_samples) {
    if (!residual || !inout_samples || count == 0 || order <= 0) return;

    for (size_t i = order; i < count; ++i) {
        int64_t sum = 0;
        for (int j = 0; j < order; ++j) {
            sum += static_cast<int64_t>(qlp_coeff[j]) * inout_samples[i - 1 - j];
        }
        int32_t pred = static_cast<int32_t>(sum >> qlp_shift);
        inout_samples[i] = residual[i] + pred;
    }
}

bool SubframeDecoder::decode_subframe(core::BitReader& reader, 
                                      int32_t* out_samples, 
                                      int32_t* scratch_residual,
                                      size_t block_size, 
                                      uint8_t bps) {
    if (!out_samples || !scratch_residual || block_size == 0 || bps == 0) return false;

    // Subframe header (RFC 9639 Section 8.3)
    uint32_t zero_bit = reader.read_bits(1);
    if (zero_bit != 0) return false; // Must be 0

    uint32_t type_code = reader.read_bits(6);
    uint32_t wasted_flag = reader.read_bits(1);
    uint8_t wasted_bits = 0;
    if (wasted_flag == 1) {
        while (reader.read_bits(1) == 0) {
            wasted_bits++;
            if (wasted_bits > 32) return false;
        }
        wasted_bits += 1;
    }

    uint8_t subframe_bps = (bps > wasted_bits) ? (bps - wasted_bits) : 0;
    if (subframe_bps == 0) return false;

    if (type_code == 0) {
        // Constant subframe
        uint32_t raw_val = reader.read_bits(subframe_bps);
        int32_t val = sign_extend(raw_val, subframe_bps);
        for (size_t i = 0; i < block_size; ++i) {
            out_samples[i] = val;
        }
    } else if (type_code == 1) {
        // Verbatim subframe
        for (size_t i = 0; i < block_size; ++i) {
            uint32_t raw_val = reader.read_bits(subframe_bps);
            out_samples[i] = sign_extend(raw_val, subframe_bps);
        }
    } else if (type_code >= 8 && type_code <= 12) {
        // Fixed predictor subframe (order 0..4)
        int order = type_code - 8;
        if (order > static_cast<int>(block_size)) return false;

        for (int i = 0; i < order; ++i) {
            uint32_t raw_val = reader.read_bits(subframe_bps);
            out_samples[i] = sign_extend(raw_val, subframe_bps);
            scratch_residual[i] = out_samples[i];
        }

        // Coded residual
        uint8_t coding_method = static_cast<uint8_t>(reader.read_bits(2));
        if (coding_method > 1) return false; // 2 & 3 reserved
        uint8_t param_bits = (coding_method == 0) ? 4 : 5;

        uint8_t partition_order = static_cast<uint8_t>(reader.read_bits(4));
        size_t num_partitions = 1u << partition_order;
        if (block_size % num_partitions != 0) return false;

        size_t samples_per_partition = block_size >> partition_order;
        if (samples_per_partition <= static_cast<size_t>(order)) return false;

        size_t res_idx = order;
        for (size_t p = 0; p < num_partitions; ++p) {
            size_t part_samples = (p == 0) ? (samples_per_partition - order) : samples_per_partition;
            if (!RiceDecoder::decode_residual_partition(reader, &scratch_residual[res_idx], part_samples, param_bits)) {
                return false;
            }
            res_idx += part_samples;
        }

        FixedPredictor::restore_samples(scratch_residual, block_size, order, out_samples);
    } else if (type_code >= 32) {
        // LPC predictor subframe (order 1..32)
        int order = (type_code - 32) + 1;
        if (order > static_cast<int>(block_size)) return false;

        for (int i = 0; i < order; ++i) {
            uint32_t raw_val = reader.read_bits(subframe_bps);
            out_samples[i] = sign_extend(raw_val, subframe_bps);
            scratch_residual[i] = out_samples[i];
        }

        uint8_t coeff_prec = static_cast<uint8_t>(reader.read_bits(4) + 1);
        if (coeff_prec == 16) return false; // 0b1111 forbidden

        uint32_t raw_shift = reader.read_bits(5);
        int32_t qlp_shift = sign_extend(raw_shift, 5);
        if (qlp_shift < 0) return false; // Must be non-negative

        int32_t qlp_coeff[32] = {0};
        for (int i = 0; i < order; ++i) {
            uint32_t raw_c = reader.read_bits(coeff_prec);
            qlp_coeff[i] = sign_extend(raw_c, coeff_prec);
        }

        // Coded residual
        uint8_t coding_method = static_cast<uint8_t>(reader.read_bits(2));
        if (coding_method > 1) return false;
        uint8_t param_bits = (coding_method == 0) ? 4 : 5;

        uint8_t partition_order = static_cast<uint8_t>(reader.read_bits(4));
        size_t num_partitions = 1u << partition_order;
        if (block_size % num_partitions != 0) return false;

        size_t samples_per_partition = block_size >> partition_order;
        if (samples_per_partition <= static_cast<size_t>(order)) return false;

        size_t res_idx = order;
        for (size_t p = 0; p < num_partitions; ++p) {
            size_t part_samples = (p == 0) ? (samples_per_partition - order) : samples_per_partition;
            if (!RiceDecoder::decode_residual_partition(reader, &scratch_residual[res_idx], part_samples, param_bits)) {
                return false;
            }
            res_idx += part_samples;
        }

        LpcPredictor::restore_samples(scratch_residual, block_size, order, qlp_coeff, qlp_shift, out_samples);
    } else {
        // Reserved subframe type
        return false;
    }

    // Restore wasted bits
    if (wasted_bits > 0) {
        for (size_t i = 0; i < block_size; ++i) {
            out_samples[i] <<= wasted_bits;
        }
    }

    return true;
}

} // namespace audio_codecs::flac
