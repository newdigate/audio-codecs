#include "src/mp3/encoder/quantizer.h"
#include "src/mp3/encoder/huffman_encoder.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace audio_codecs::mp3 {

void Quantizer::quantize_granule(const float* xr_576, 
                                 const float* mask_thresholds_22,
                                 const FrameHeader& header, 
                                 GranuleChannelInfo& gi, 
                                 ScalefactorData& sf, 
                                 int16_t* is_out_576, 
                                 size_t target_bits) {
    if (!xr_576 || !is_out_576) return;

    gi.window_switching_flag = false;
    gi.block_type = 0;
    gi.mixed_block_flag = false;
    gi.scalefac_scale = false;
    gi.preflag = false;
    gi.scalefac_compress = 0; // slen1=0, slen2=0

    std::memset(sf.l, 0, sizeof(sf.l));

    // Rate-control binary search over global_gain (0..255)
    uint8_t low_gain = 0;
    uint8_t high_gain = 255;
    uint8_t best_gain = 210;

    int16_t temp_is[576];

    for (int iter = 0; iter < 12; ++iter) {
        uint8_t mid_gain = (low_gain + high_gain) / 2;
        float step = std::pow(2.0f, 0.25f * (static_cast<float>(mid_gain) - 210.0f));

        // Quantize: ix = int((|xr| / step)^0.75 - 0.0946)
        int last_nonzero = -1;
        for (int i = 0; i < 576; ++i) {
            float abs_x = std::abs(xr_576[i]);
            if (abs_x < 1e-9f || step < 1e-9f) {
                temp_is[i] = 0;
            } else {
                float val = std::pow(abs_x / step, 0.75f) - 0.0946f;
                int16_t ix = (val > 0.0f) ? static_cast<int16_t>(val + 0.5f) : 0;
                ix = std::min(ix, static_cast<int16_t>(8191)); // Max big_values
                temp_is[i] = (xr_576[i] < 0) ? -ix : ix;
            }
            if (temp_is[i] != 0) {
                last_nonzero = i;
            }
        }

        // Partition spectrum: big_values, count1, rzero
        int num_lines = (last_nonzero >= 0) ? (last_nonzero + 1) : 0;
        if (num_lines & 1) num_lines++; // Must be multiple of 2

        int big_vals = num_lines / 2;
        big_vals = std::min(big_vals, 288);

        // Region boundaries: single region for now
        int t0 = HuffmanEncoder::choose_optimal_table(temp_is, big_vals * 2);
        size_t bits = HuffmanEncoder::count_bits_pairs(temp_is, big_vals * 2, t0);

        if (bits <= target_bits) {
            best_gain = mid_gain;
            high_gain = mid_gain; // Try lower gain (higher quality / more bits)
            gi.big_values = static_cast<uint16_t>(big_vals);
            gi.table_select[0] = static_cast<uint8_t>(t0);
            gi.table_select[1] = 0;
            gi.table_select[2] = 0;
            gi.region0_count = 21; // whole spectrum in region 0
            gi.region1_count = 0;
            gi.count1table_select = false;
            gi.part2_3_length = static_cast<uint16_t>(bits);
            std::memcpy(is_out_576, temp_is, sizeof(temp_is));
        } else {
            low_gain = mid_gain + 1; // Need higher gain (fewer bits)
        }
    }

    gi.global_gain = best_gain;
}

} // namespace audio_codecs::mp3
