#include "src/flac/decoder/rice_decoder.h"

namespace audio_codecs::flac {

bool RiceDecoder::decode_residual_partition(core::BitReader& reader, 
                                            int32_t* out_residual, 
                                            size_t count, 
                                            uint8_t rice_param_bits) {
    if (!out_residual) return false;

    uint8_t param = static_cast<uint8_t>(reader.read_bits(rice_param_bits));
    uint8_t escape_code = (rice_param_bits == 4) ? 0x0F : 0x1F;
    if (param == escape_code) {
        return decode_escaped_partition(reader, out_residual, count);
    }

    for (size_t i = 0; i < count; ++i) {
        // Read unary quotient: count zeros until 1 bit
        uint32_t msbs = 0;
        while (true) {
            uint32_t bit = reader.read_bits(1);
            if (bit == 1) {
                break;
            }
            msbs++;
            if (msbs > 65536) { // Safeguard against corrupted bitstream
                return false;
            }
        }

        uint32_t lsbs = 0;
        if (param > 0) {
            lsbs = reader.read_bits(param);
        }

        uint32_t u = (msbs << param) | lsbs;
        out_residual[i] = unfold(u);
    }

    return true;
}

bool RiceDecoder::decode_escaped_partition(core::BitReader& reader, 
                                           int32_t* out_residual, 
                                           size_t count) {
    if (!out_residual) return false;

    uint8_t raw_bits = static_cast<uint8_t>(reader.read_bits(5));
    if (raw_bits == 0) {
        for (size_t i = 0; i < count; ++i) {
            out_residual[i] = 0;
        }
        return true;
    }

    if (raw_bits > 32) return false;

    for (size_t i = 0; i < count; ++i) {
        uint32_t raw = reader.read_bits(raw_bits);
        // Sign-extend two's complement
        if (raw_bits < 32 && (raw & (1u << (raw_bits - 1)))) {
            raw |= (~0u << raw_bits);
        }
        out_residual[i] = static_cast<int32_t>(raw);
    }

    return true;
}

} // namespace audio_codecs::flac
