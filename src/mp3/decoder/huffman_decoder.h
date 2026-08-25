#pragma once
#include "src/core/bit_reader.h"
#include "src/mp3/mp3_common.h"
#include "src/mp3/mp3_tables.h"
#include <cstdint>

namespace audio_codecs::mp3 {

class HuffmanDecoder {
public:
    // Decode spectral values (576 lines) for one granule/channel
    static bool decode_granule(core::BitReader& reader, 
                               const GranuleChannelInfo& gi, 
                               const FrameHeader& header, 
                               int16_t* is_out_576, 
                               size_t part3_bits);

private:
    static bool decode_pair(core::BitReader& reader, 
                            const HuffmanCodebook& book, 
                            int16_t& x_out, 
                            int16_t& y_out);

    static bool decode_quad(core::BitReader& reader, 
                            bool table_b, 
                            int16_t& v_out, 
                            int16_t& w_out, 
                            int16_t& x_out, 
                            int16_t& y_out);
};

} // namespace audio_codecs::mp3
