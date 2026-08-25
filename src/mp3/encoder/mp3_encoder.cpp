#include "audio_codecs/mp3/mp3_encoder.h"
#include "src/mp3/mp3_common.h"
#include "src/mp3/encoder/analysis_filter.h"
#include "src/mp3/encoder/mdct.h"
#include "src/mp3/encoder/psychoacoustic.h"
#include "src/mp3/encoder/quantizer.h"
#include "src/mp3/encoder/huffman_encoder.h"
#include "src/core/bit_writer.h"
#include <new>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace audio_codecs::mp3 {

struct Mp3Encoder::Impl {
    AudioConfig config;
    FrameHeader header;

    AnalysisFilter analysis[2];
    ForwardMdct mdct;
    PsychoacousticModel psycho;

    float pcm_history[2][1024];
    float subband_history[2][32][36];

    float xr[2][2][576]; // [gr][ch][576]
    int16_t is[2][2][576];
    ScalefactorData sf[2][2];
    SideInfo side;

    void reset() {
        std::memset(pcm_history, 0, sizeof(pcm_history));
        std::memset(subband_history, 0, sizeof(subband_history));
        std::memset(&side, 0, sizeof(side));
        analysis[0].reset();
        analysis[1].reset();
        mdct.reset();
        psycho.reset();
    }
};

Mp3Encoder::Mp3Encoder() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "Mp3Encoder state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
    reset();
}

Mp3Encoder::~Mp3Encoder() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

bool Mp3Encoder::init(const AudioConfig& config) {
    if (!impl_) {
        impl_ = new (state_buffer_) Impl();
    }
    impl_->config = config;

    // Configure header
    impl_->header.version = (config.sample_rate >= 32000) ? MpegVersion::Mpeg1 :
                            (config.sample_rate >= 16000) ? MpegVersion::Mpeg2 : MpegVersion::Mpeg25;
    impl_->header.layer = MpegLayer::Layer3;
    impl_->header.protection_bit = true; // No CRC
    impl_->header.sample_rate = config.sample_rate;
    impl_->header.channels = config.channels;
    impl_->header.mode = (config.channels == 1) ? MpegMode::SingleChannel : MpegMode::Stereo;
    impl_->header.mode_extension = 0;
    impl_->header.intensity_stereo = false;
    impl_->header.ms_stereo = false;
    impl_->header.padding_bit = false;
    impl_->header.private_bit = false;
    impl_->header.copyright = false;
    impl_->header.original = true;
    impl_->header.emphasis = MpegEmphasis::None;

    // Find sampling frequency index
    if (config.sample_rate == 44100 || config.sample_rate == 22050 || config.sample_rate == 11025) {
        impl_->header.sampling_frequency = 0;
    } else if (config.sample_rate == 48000 || config.sample_rate == 24000 || config.sample_rate == 12000) {
        impl_->header.sampling_frequency = 1;
    } else {
        impl_->header.sampling_frequency = 2; // 32000 / 16000 / 8000
    }

    // Match nearest standard bitrate
    static const uint16_t MPEG1_BITRATES[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    uint32_t target_kbps = config.bitrate_kbps;
    uint8_t best_idx = 9; // Default 128 kbps
    uint32_t min_diff = 1000;

    for (uint8_t idx = 1; idx <= 14; ++idx) {
        uint32_t br = MPEG1_BITRATES[idx - 1];
        uint32_t diff = (br > target_kbps) ? (br - target_kbps) : (target_kbps - br);
        if (diff < min_diff) {
            min_diff = diff;
            best_idx = idx;
        }
    }

    impl_->header.bitrate_index = best_idx;
    impl_->header.bitrate_kbps = MPEG1_BITRATES[best_idx - 1];
    impl_->header.ngr = (impl_->header.version == MpegVersion::Mpeg1) ? 2 : 1;

    // Calculate frame size in bytes
    if (impl_->header.version == MpegVersion::Mpeg1) {
        impl_->header.frame_bytes = (144000 * impl_->header.bitrate_kbps) / impl_->header.sample_rate;
        impl_->header.side_info_bytes = (impl_->header.channels == 1) ? 17 : 32;
    } else {
        impl_->header.frame_bytes = (72000 * impl_->header.bitrate_kbps) / impl_->header.sample_rate;
        impl_->header.side_info_bytes = (impl_->header.channels == 1) ? 9 : 17;
    }

    impl_->psycho.init(config.sample_rate);
    reset();
    return true;
}

void Mp3Encoder::reset() {
    if (impl_) {
        impl_->reset();
    }
}

int Mp3Encoder::flush(uint8_t* out_data, size_t max_out_bytes) {
    return 0; // Stateless frame flushing
}

int Mp3Encoder::encode_frame(const float* in_pcm, size_t in_samples,
                             uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data || max_out_bytes < impl_->header.frame_bytes) {
        return -1;
    }

    size_t samples_per_channel = impl_->header.ngr * 576;
    size_t expected_samples = samples_per_channel * impl_->header.channels;
    if (in_samples < expected_samples) {
        return -2;
    }

    // 1. De-interleave PCM and compute Psychoacoustic masking per channel
    float mask_thresholds[2][22];
    float smr[2][22];

    for (int ch = 0; ch < impl_->header.channels; ++ch) {
        // Shift history and copy latest 1024 samples
        std::memmove(impl_->pcm_history[ch], impl_->pcm_history[ch] + 576, (1024 - 576) * sizeof(float));
        for (int i = 0; i < 576; ++i) {
            impl_->pcm_history[ch][1024 - 576 + i] = in_pcm[i * impl_->header.channels + ch];
        }
        impl_->psycho.calculate_masking(impl_->pcm_history[ch], mask_thresholds[ch], smr[ch]);
    }

    // 2. Polyphase Analysis Filterbank & Forward MDCT for each granule
    for (int gr = 0; gr < impl_->header.ngr; ++gr) {
        size_t gr_offset = gr * 576;

        for (int ch = 0; ch < impl_->header.channels; ++ch) {
            // Process 18 time steps of 32 PCM samples = 576 samples
            for (int t = 0; t < 18; ++t) {
                float pcm_32[32];
                for (int i = 0; i < 32; ++i) {
                    pcm_32[i] = in_pcm[(gr_offset + t * 32 + i) * impl_->header.channels + ch];
                }

                float subband_32[32];
                impl_->analysis[ch].filter_pcm(pcm_32, subband_32);

                for (int sb = 0; sb < 32; ++sb) {
                    float s_val = subband_32[sb];
                    // Frequency inversion compensation for odd subbands and odd time steps
                    if ((sb & 1) && (t & 1)) {
                        s_val = -s_val;
                    }
                    // Shift subband history and insert new time sample
                    std::memmove(&impl_->subband_history[ch][sb][0], &impl_->subband_history[ch][sb][1], 35 * sizeof(float));
                    impl_->subband_history[ch][sb][35] = s_val;
                }
            }

            // Transform each subband with 36-point forward MDCT
            for (int sb = 0; sb < 32; ++sb) {
                float mdct_18[18];
                impl_->mdct.transform_subband(impl_->subband_history[ch][sb], mdct_18, 0);

                for (int k = 0; k < 18; ++k) {
                    impl_->xr[gr][ch][sb * 18 + k] = mdct_18[k];
                }
            }
        }
    }

    // 3. Quantization & Bit Budget Allocation
    size_t header_len = 4;
    size_t static_headers_len = header_len + impl_->header.side_info_bytes;
    size_t available_main_bytes = (impl_->header.frame_bytes > static_headers_len) ? 
                                  (impl_->header.frame_bytes - static_headers_len) : 0;
    size_t target_bits_per_gr_ch = (available_main_bytes * 8) / (impl_->header.ngr * impl_->header.channels);

    // If VBR, scale bit target based on quality and SMR
    if (impl_->config.vbr) {
        float avg_smr = 0.0f;
        for (int s = 0; s < 22; ++s) avg_smr += smr[0][s];
        avg_smr /= 22.0f;
        float vbr_scale = 1.0f + (avg_smr / 50.0f) * (1.0f - impl_->config.vbr_quality * 0.1f);
        target_bits_per_gr_ch = static_cast<size_t>(target_bits_per_gr_ch * std::clamp(vbr_scale, 0.5f, 1.5f));
    }

    for (int gr = 0; gr < impl_->header.ngr; ++gr) {
        for (int ch = 0; ch < impl_->header.channels; ++ch) {
            Quantizer::quantize_granule(impl_->xr[gr][ch], mask_thresholds[ch], 
                                        impl_->header, impl_->side.gr[gr][ch], 
                                        impl_->sf[gr][ch], impl_->is[gr][ch], 
                                        target_bits_per_gr_ch);
        }
    }

    // 4. Bitstream Assembly
    std::memset(out_data, 0, impl_->header.frame_bytes);

    // A. Write 32-bit Frame Header
    uint32_t header_word = 0;
    build_frame_header_word(impl_->header, header_word);
    out_data[0] = static_cast<uint8_t>((header_word >> 24) & 0xFF);
    out_data[1] = static_cast<uint8_t>((header_word >> 16) & 0xFF);
    out_data[2] = static_cast<uint8_t>((header_word >> 8) & 0xFF);
    out_data[3] = static_cast<uint8_t>(header_word & 0xFF);

    // B. Write Side Information
    core::BitWriter side_writer;
    side_writer.init(out_data + header_len, impl_->header.side_info_bytes);

    side_writer.write_bits(0, (impl_->header.version == MpegVersion::Mpeg1 ? 9 : 8)); // main_data_begin = 0
    side_writer.write_bits(0, (impl_->header.channels == 1 ? (impl_->header.version == MpegVersion::Mpeg1 ? 5 : 1) :
                                                             (impl_->header.version == MpegVersion::Mpeg1 ? 3 : 2)));

    if (impl_->header.version == MpegVersion::Mpeg1) {
        for (int ch = 0; ch < impl_->header.channels; ++ch) {
            for (int band = 0; band < 4; ++band) {
                side_writer.write_bits(0, 1); // scfsi = 0
            }
        }
    }

    for (int gr = 0; gr < impl_->header.ngr; ++gr) {
        for (int ch = 0; ch < impl_->header.channels; ++ch) {
            const GranuleChannelInfo& gi = impl_->side.gr[gr][ch];
            side_writer.write_bits(gi.part2_3_length, 12);
            side_writer.write_bits(gi.big_values, 9);
            side_writer.write_bits(gi.global_gain, 8);
            side_writer.write_bits(gi.scalefac_compress, (impl_->header.version == MpegVersion::Mpeg1 ? 4 : 9));
            side_writer.write_bits(gi.window_switching_flag ? 1 : 0, 1);

            for (int region = 0; region < 3; ++region) {
                side_writer.write_bits(gi.table_select[region], 5);
            }
            side_writer.write_bits(gi.region0_count, 4);
            side_writer.write_bits(gi.region1_count, 3);

            if (impl_->header.version == MpegVersion::Mpeg1) {
                side_writer.write_bits(gi.preflag ? 1 : 0, 1);
            }
            side_writer.write_bits(gi.scalefac_scale ? 1 : 0, 1);
            side_writer.write_bits(gi.count1table_select ? 1 : 0, 1);
        }
    }

    // C. Write Main Data (Huffman bits)
    core::BitWriter main_writer;
    main_writer.init(out_data + static_headers_len, available_main_bytes);

    for (int gr = 0; gr < impl_->header.ngr; ++gr) {
        for (int ch = 0; ch < impl_->header.channels; ++ch) {
            const GranuleChannelInfo& gi = impl_->side.gr[gr][ch];
            if (gi.big_values > 0 && gi.table_select[0] > 0) {
                HuffmanEncoder::encode_pairs(main_writer, impl_->is[gr][ch], gi.big_values * 2, gi.table_select[0]);
            }
        }
    }

    // D. Pad remaining frame with 0x00 stuffing bytes
    main_writer.flush_to_byte();

    return static_cast<int>(impl_->header.frame_bytes);
}

} // namespace audio_codecs::mp3
