#include "include/audio_codecs/aac/aac_encoder.h"
#include "include/audio_codecs/aac/adts_header.h"
#include "src/aac/adts_parser.h"
#include "src/aac/aac_tables.h"
#include "src/aac/aac_mdct.h"
#include "src/aac/encoder/transient_detector.h"
#include "src/aac/encoder/psychoacoustic.h"
#include "src/aac/encoder/quantizer.h"
#include "src/aac/encoder/huffman_encoder.h"
#include "src/aac/decoder/huffman_decoder.h"
#include "src/core/bit_writer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <new>

namespace audio_codecs::aac {

struct AacEncoder::Impl {
    AudioConfig config;
    AacMdct mdct;
    AacQuantizer quantizer;
    TransientDetector transient[2];
    PsychoacousticModel psycho[2];

    // History buffer: 2048 samples per channel
    alignas(16) float pcm_history[2][2048]{{0.0f}};
    WindowSequence last_window_seq[2]{WindowSequence::OnlyLong, WindowSequence::OnlyLong};
    WindowShape last_window_shape[2]{WindowShape::Sine, WindowShape::Sine};

    // Scratch buffers for encoding one frame
    alignas(16) float mdct_spec[2][1024]{{0.0f}};
    alignas(16) float thresholds[2][AAC_MAX_SCALEFACTOR_BANDS]{{0.0f}};
    alignas(16) float energy[2][AAC_MAX_SCALEFACTOR_BANDS]{{0.0f}};
    int quant[2][1024]{{0}};
    int grouped_quant[2][1024]{{0}}; // For EightShort grouped coefficients
    int sf[2][AAC_MAX_SCALEFACTOR_BANDS]{{0}};
    int global_gain[2]{100, 100};
    int band_cb[2][AAC_MAX_SCALEFACTOR_BANDS]{{0}};
    int sect_cb[2][AAC_MAX_SCALEFACTOR_BANDS]{{0}};
    int sect_len[2][AAC_MAX_SCALEFACTOR_BANDS]{{0}};
    size_t num_sections[2]{0, 0};
    uint8_t max_sfb[2]{0, 0};
    WindowSequence decided_seq[2]{WindowSequence::OnlyLong, WindowSequence::OnlyLong};

    alignas(16) uint8_t payload_buffer[8192]{0};

    void reset() {
        std::memset(pcm_history, 0, sizeof(pcm_history));
        last_window_seq[0] = WindowSequence::OnlyLong;
        last_window_seq[1] = WindowSequence::OnlyLong;
        last_window_shape[0] = WindowShape::Sine;
        last_window_shape[1] = WindowShape::Sine;
        transient[0].reset();
        transient[1].reset();
        psycho[0].reset();
        psycho[1].reset();
    }

    void quantize_eight_short_channel(uint8_t c,
                                      const int* swb_offsets_short,
                                      size_t num_swb_short) {
        int sf_temp[AAC_MAX_SCALEFACTOR_BANDS] = {0};
        bool active[AAC_MAX_SCALEFACTOR_BANDS] = {false};

        for (size_t s = 0; s < num_swb_short && s < AAC_MAX_SCALEFACTOR_BANDS; ++s) {
            int start = swb_offsets_short[s];
            int end = swb_offsets_short[s + 1];
            int width = end - start;
            float total_width = static_cast<float>(width * 8);

            float band_energy = 0.0f;
            float max_val = 0.0f;
            for (int w = 0; w < 8; ++w) {
                for (int i = start; i < end; ++i) {
                    float ax = std::fabs(mdct_spec[c][w * 128 + i]);
                    if (ax > max_val) max_val = ax;
                    band_energy += ax * ax;
                }
            }

            if (max_val < 0.05f) {
                sf_temp[s] = 0;
                active[s] = false;
                for (int w = 0; w < 8; ++w) {
                    for (int i = start; i < end; ++i) {
                        quant[c][w * 128 + i] = 0;
                    }
                }
                continue;
            }

            active[s] = true;
            float est_sf = 100.0f + 4.0f * (std::log(std::max(max_val / 3000.0f, 1e-12f)) / std::log(2.0f));
            float min_sf_for_max = 100.0f + 4.0f * (std::log(std::max(max_val / 160000.0f, 1e-12f)) / std::log(2.0f));
            est_sf = std::max(min_sf_for_max, est_sf);

            int isf = static_cast<int>(std::round(est_sf));
            sf_temp[s] = std::max(0, std::min(255, isf));
        }

        int first_active = -1;
        for (size_t s = 0; s < num_swb_short; ++s) {
            if (active[s]) {
                first_active = static_cast<int>(s);
                break;
            }
        }

        if (first_active == -1) {
            global_gain[c] = 100;
            for (size_t s = 0; s < num_swb_short; ++s) {
                sf[c][s] = 0;
                band_cb[c][s] = HCB_ZERO;
            }
            max_sfb[c] = 0;
            return;
        }

        global_gain[c] = sf_temp[first_active];
        int last_sf = global_gain[c];

        for (size_t s = 0; s < num_swb_short; ++s) {
            if (active[s]) {
                int delta = sf_temp[s] - last_sf;
                if (delta > 60) sf_temp[s] = last_sf + 60;
                if (delta < -60) sf_temp[s] = last_sf - 60;
                sf_temp[s] = std::max(0, std::min(255, sf_temp[s]));
                last_sf = sf_temp[s];
            }
            sf[c][s] = sf_temp[s];
        }

        int last_active_band = -1;
        size_t g_idx = 0;

        for (size_t s = 0; s < num_swb_short; ++s) {
            int start = swb_offsets_short[s];
            int end = swb_offsets_short[s + 1];
            int width = end - start;
            int total_lines = width * 8;

            if (!active[s]) {
                for (int w = 0; w < 8; ++w) {
                    for (int i = 0; i < width; ++i) {
                        quant[c][w * 128 + start + i] = 0;
                    }
                }
                for (int i = 0; i < total_lines; ++i) {
                    grouped_quant[c][g_idx + i] = 0;
                }
                band_cb[c][s] = HCB_ZERO;
            } else {
                int s_val = sf[c][s];
                for (int w = 0; w < 8; ++w) {
                    for (int i = 0; i < width; ++i) {
                        int q = AacQuantizer::quantize_single(mdct_spec[c][w * 128 + start + i], s_val);
                        quant[c][w * 128 + start + i] = q;
                        grouped_quant[c][g_idx + w * width + i] = q;
                    }
                }
                band_cb[c][s] = AacHuffmanEncoder::find_best_codebook_for_band(&grouped_quant[c][g_idx], total_lines);
                if (band_cb[c][s] != HCB_ZERO) {
                    last_active_band = static_cast<int>(s);
                }
            }
            g_idx += total_lines;
        }

        max_sfb[c] = (last_active_band >= 0) ? static_cast<uint8_t>(last_active_band + 1) : 0;
    }
};

AacEncoder::AacEncoder() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "AacEncoder state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
    reset();
}

AacEncoder::~AacEncoder() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

bool AacEncoder::init(const AudioConfig& config) {
    if (!impl_) {
        impl_ = new (state_buffer_) Impl();
    }

    if (config.channels != 1 && config.channels != 2) {
        return false;
    }

    int sf_index = get_sampling_frequency_index(config.sample_rate);
    if (sf_index < 0) {
        return false;
    }

    impl_->config = config;
    impl_->psycho[0].init(config.sample_rate);
    impl_->psycho[1].init(config.sample_rate);

    reset();
    return true;
}

void AacEncoder::reset() {
    if (impl_) {
        impl_->reset();
    }
}

const AudioConfig& AacEncoder::get_config() const {
    return impl_->config;
}

int AacEncoder::flush(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !out_data || max_out_bytes < 13) {
        return 0;
    }

    // Flush remaining delayed frame with zeros
    float zeros[2048] = {0.0f};
    return encode_frame(zeros, impl_->config.channels * 1024, out_data, max_out_bytes);
}

int AacEncoder::encode_frame(const float* in_pcm, size_t in_samples,
                             uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || !out_data) {
        return -1;
    }

    uint8_t channels = impl_->config.channels;
    if (channels != 1 && channels != 2) {
        return -1;
    }

    size_t min_in_samples = static_cast<size_t>(channels) * 1024;
    if (in_samples < min_in_samples) {
        return -2;
    }

    // 1. Shift history and copy new samples per channel
    for (uint8_t c = 0; c < channels; ++c) {
        std::memmove(impl_->pcm_history[c], impl_->pcm_history[c] + 1024, 1024 * sizeof(float));
        for (size_t i = 0; i < 1024; ++i) {
            impl_->pcm_history[c][1024 + i] = in_pcm[i * channels + c];
        }
    }

    // 2. Window Sequence Decision per channel
    for (uint8_t c = 0; c < channels; ++c) {
        impl_->decided_seq[c] = impl_->transient[c].update(impl_->pcm_history[c]);
    }

    if (channels == 2) {
        if (impl_->decided_seq[0] != impl_->decided_seq[1]) {
            if (impl_->decided_seq[0] == WindowSequence::LongStart || impl_->decided_seq[1] == WindowSequence::LongStart) {
                impl_->decided_seq[0] = WindowSequence::LongStart;
                impl_->decided_seq[1] = WindowSequence::LongStart;
            } else if (impl_->decided_seq[0] == WindowSequence::EightShort || impl_->decided_seq[1] == WindowSequence::EightShort) {
                impl_->decided_seq[0] = WindowSequence::EightShort;
                impl_->decided_seq[1] = WindowSequence::EightShort;
            } else if (impl_->decided_seq[0] == WindowSequence::LongStop || impl_->decided_seq[1] == WindowSequence::LongStop) {
                impl_->decided_seq[0] = WindowSequence::LongStop;
                impl_->decided_seq[1] = WindowSequence::LongStop;
            }
        }
    }

    // 3. Bitrate and Band Offsets
    int target_bits_per_channel = 0;
    if (impl_->config.bitrate_kbps > 0 && !impl_->config.vbr) {
        int total_target_bits = static_cast<int>((static_cast<uint64_t>(impl_->config.bitrate_kbps) * 1000 * 1024) / impl_->config.sample_rate);
        target_bits_per_channel = std::max(100, (total_target_bits - 80) / channels);
    }

    int sf_index = get_sampling_frequency_index(impl_->config.sample_rate);
    if (sf_index < 0) sf_index = 4;

    size_t num_swb_long = 0;
    const int* swb_offsets_long = get_swb_offset_long_index(sf_index, num_swb_long);
    size_t num_swb_short = 0;
    const int* swb_offsets_short = get_swb_offset_short_index(sf_index, num_swb_short);

    if (!swb_offsets_long || !swb_offsets_short) {
        return -3;
    }

    // 4. Psychoacoustic Analysis, MDCT, and Quantization per channel
    for (uint8_t c = 0; c < channels; ++c) {
        WindowSequence seq = impl_->decided_seq[c];
        float pe = 0.0f;

        if (seq == WindowSequence::EightShort) {
            impl_->psycho[c].analyze_short(&impl_->pcm_history[c][448 + 7 * 128], impl_->config.sample_rate,
                                           impl_->thresholds[c], impl_->energy[c], pe);
        } else {
            impl_->psycho[c].analyze_long(impl_->pcm_history[c], impl_->config.sample_rate,
                                          impl_->thresholds[c], impl_->energy[c], pe);
        }

        // Forward MDCT
        impl_->mdct.forward_windowed(impl_->pcm_history[c], impl_->mdct_spec[c],
                                     seq, impl_->last_window_shape[c], WindowShape::Sine);
        impl_->last_window_seq[c] = seq;
        impl_->last_window_shape[c] = WindowShape::Sine;

        if (seq == WindowSequence::EightShort) {
            impl_->quantize_eight_short_channel(c, swb_offsets_short, num_swb_short);
        } else {
            impl_->quantizer.quantize_spectrum_fast(impl_->mdct_spec[c], impl_->thresholds[c],
                                                   swb_offsets_long, num_swb_long,
                                                   impl_->quant[c], impl_->sf[c], impl_->global_gain[c],
                                                   target_bits_per_channel);

            int last_active_band = -1;
            for (size_t b = 0; b < num_swb_long; ++b) {
                int start = swb_offsets_long[b];
                int width = swb_offsets_long[b + 1] - start;
                impl_->band_cb[c][b] = AacHuffmanEncoder::find_best_codebook_for_band(&impl_->quant[c][start], width);
                if (impl_->band_cb[c][b] != HCB_ZERO) {
                    last_active_band = static_cast<int>(b);
                }
            }
            impl_->max_sfb[c] = (last_active_band >= 0) ? static_cast<uint8_t>(last_active_band + 1) : 0;
        }
    }

    // 5. Bitstream Formatting
    core::BitWriter pw;
    pw.init(impl_->payload_buffer, sizeof(impl_->payload_buffer));

    if (channels == 1) {
        // Mono (SCE)
        pw.write_bits(static_cast<uint32_t>(ElementId::SCE), 3);
        pw.write_bits(0, 4); // element_instance_tag

        uint8_t max_sfb = impl_->max_sfb[0];
        WindowSequence seq = impl_->decided_seq[0];
        bool is_short = (seq == WindowSequence::EightShort);
        const int* swb = is_short ? swb_offsets_short : swb_offsets_long;

        pw.write_bits(static_cast<uint32_t>(impl_->global_gain[0]), 8);

        // ics_info
        pw.write_bits(0, 1); // reserved
        pw.write_bits(static_cast<uint32_t>(seq), 2);
        pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
        if (is_short) {
            pw.write_bits(max_sfb, 4);
            pw.write_bits(0x7F, 7); // scale_factor_grouping = 0x7F (1 group of 8 windows)
        } else {
            pw.write_bits(max_sfb, 6);
            pw.write_bits(0, 1); // predictor_data_present
        }

        if (max_sfb > 0) {
            // Section data
            AacHuffmanEncoder::build_sections(impl_->band_cb[0], max_sfb,
                                              impl_->sect_cb[0], impl_->sect_len[0], impl_->num_sections[0]);
            AacHuffmanEncoder::write_section_data(pw, impl_->sect_cb[0], impl_->sect_len[0],
                                                 impl_->num_sections[0], is_short);

            // Scalefactor data
            AacHuffmanEncoder::write_scalefactor_data(pw, impl_->global_gain[0], impl_->sf[0],
                                                     impl_->band_cb[0], max_sfb);
        }

        pw.write_bits(0, 1); // pulse_data_present
        pw.write_bits(0, 1); // tns_data_present
        pw.write_bits(0, 1); // gain_control_data_present

        if (max_sfb > 0) {
            // Spectral data
            if (is_short) {
                size_t g_idx = 0;
                for (size_t b = 0; b < max_sfb; ++b) {
                    int cb = impl_->band_cb[0][b];
                    int width = swb[b + 1] - swb[b];
                    int total_lines = width * 8;
                    if (cb != HCB_ZERO) {
                        AacHuffmanEncoder::encode_spectral_data_band(pw, cb, &impl_->grouped_quant[0][g_idx], total_lines);
                    }
                    g_idx += total_lines;
                }
            } else {
                AacHuffmanEncoder::write_spectral_data(pw, impl_->quant[0], impl_->band_cb[0], swb, max_sfb);
            }
        }
    } else {
        // Stereo (CPE)
        pw.write_bits(static_cast<uint32_t>(ElementId::CPE), 3);
        pw.write_bits(0, 4); // element_instance_tag

        bool common_window = (impl_->decided_seq[0] == impl_->decided_seq[1]);
        pw.write_bits(common_window ? 1 : 0, 1);

        uint8_t shared_max_sfb = std::max(impl_->max_sfb[0], impl_->max_sfb[1]);
        WindowSequence seq = impl_->decided_seq[0];
        bool is_short = (seq == WindowSequence::EightShort);
        const int* swb = is_short ? swb_offsets_short : swb_offsets_long;

        if (common_window) {
            // Shared ics_info
            pw.write_bits(0, 1); // reserved
            pw.write_bits(static_cast<uint32_t>(seq), 2);
            pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
            if (is_short) {
                pw.write_bits(shared_max_sfb, 4);
                pw.write_bits(0x7F, 7); // grouping = 0x7F
            } else {
                pw.write_bits(shared_max_sfb, 6);
                pw.write_bits(0, 1); // predictor
            }

            // ms_mask_present = 0
            pw.write_bits(0, 2);

            for (uint8_t c = 0; c < 2; ++c) {
                pw.write_bits(static_cast<uint32_t>(impl_->global_gain[c]), 8);

                if (shared_max_sfb > 0) {
                    AacHuffmanEncoder::build_sections(impl_->band_cb[c], shared_max_sfb,
                                                      impl_->sect_cb[c], impl_->sect_len[c], impl_->num_sections[c]);
                    AacHuffmanEncoder::write_section_data(pw, impl_->sect_cb[c], impl_->sect_len[c],
                                                         impl_->num_sections[c], is_short);

                    AacHuffmanEncoder::write_scalefactor_data(pw, impl_->global_gain[c], impl_->sf[c],
                                                             impl_->band_cb[c], shared_max_sfb);
                }

                pw.write_bits(0, 1); // pulse
                pw.write_bits(0, 1); // tns
                pw.write_bits(0, 1); // gain

                if (shared_max_sfb > 0) {
                    if (is_short) {
                        size_t g_idx = 0;
                        for (size_t b = 0; b < shared_max_sfb; ++b) {
                            int cb = impl_->band_cb[c][b];
                            int width = swb[b + 1] - swb[b];
                            int total_lines = width * 8;
                            if (cb != HCB_ZERO) {
                                AacHuffmanEncoder::encode_spectral_data_band(pw, cb, &impl_->grouped_quant[c][g_idx], total_lines);
                            }
                            g_idx += total_lines;
                        }
                    } else {
                        AacHuffmanEncoder::write_spectral_data(pw, impl_->quant[c], impl_->band_cb[c], swb, shared_max_sfb);
                    }
                }
            }
        } else {
            // Separate windows
            for (uint8_t c = 0; c < 2; ++c) {
                uint8_t max_sfb_c = impl_->max_sfb[c];
                WindowSequence seq_c = impl_->decided_seq[c];
                bool is_short_c = (seq_c == WindowSequence::EightShort);
                const int* swb_c = is_short_c ? swb_offsets_short : swb_offsets_long;

                pw.write_bits(static_cast<uint32_t>(impl_->global_gain[c]), 8);

                // ics_info
                pw.write_bits(0, 1);
                pw.write_bits(static_cast<uint32_t>(seq_c), 2);
                pw.write_bits(static_cast<uint32_t>(WindowShape::Sine), 1);
                if (is_short_c) {
                    pw.write_bits(max_sfb_c, 4);
                    pw.write_bits(0x7F, 7);
                } else {
                    pw.write_bits(max_sfb_c, 6);
                    pw.write_bits(0, 1);
                }

                if (max_sfb_c > 0) {
                    AacHuffmanEncoder::build_sections(impl_->band_cb[c], max_sfb_c,
                                                      impl_->sect_cb[c], impl_->sect_len[c], impl_->num_sections[c]);
                    AacHuffmanEncoder::write_section_data(pw, impl_->sect_cb[c], impl_->sect_len[c],
                                                         impl_->num_sections[c], is_short_c);

                    AacHuffmanEncoder::write_scalefactor_data(pw, impl_->global_gain[c], impl_->sf[c],
                                                             impl_->band_cb[c], max_sfb_c);
                }

                pw.write_bits(0, 1);
                pw.write_bits(0, 1);
                pw.write_bits(0, 1);

                if (max_sfb_c > 0) {
                    if (is_short_c) {
                        size_t g_idx = 0;
                        for (size_t b = 0; b < max_sfb_c; ++b) {
                            int cb = impl_->band_cb[c][b];
                            int width = swb_c[b + 1] - swb_c[b];
                            int total_lines = width * 8;
                            if (cb != HCB_ZERO) {
                                AacHuffmanEncoder::encode_spectral_data_band(pw, cb, &impl_->grouped_quant[c][g_idx], total_lines);
                            }
                            g_idx += total_lines;
                        }
                    } else {
                        AacHuffmanEncoder::write_spectral_data(pw, impl_->quant[c], impl_->band_cb[c], swb_c, max_sfb_c);
                    }
                }
            }
        }
    }

    // ID_END (7)
    pw.write_bits(static_cast<uint32_t>(ElementId::END), 3);
    pw.flush_to_byte();

    size_t payload_len = pw.get_byte_count();
    size_t header_len = 7;
    size_t total_frame_bytes = header_len + payload_len;

    if (total_frame_bytes > max_out_bytes) {
        return -3;
    }

    AdtsHeader header;
    header.syncword = 0xFFF;
    header.id = 0; // MPEG-4
    header.layer = 0;
    header.protection_absent = true;
    header.profile = 1; // AAC-LC
    header.sampling_frequency_index = static_cast<uint8_t>(sf_index);
    header.sample_rate = impl_->config.sample_rate;
    header.channel_configuration = channels;
    header.frame_length = static_cast<uint16_t>(total_frame_bytes);
    header.adts_buffer_fullness = 0x7FF;
    header.num_raw_data_blocks = 0;

    core::BitWriter hw;
    hw.init(out_data, total_frame_bytes);
    write_adts_header(hw, header);

    std::memcpy(out_data + header_len, impl_->payload_buffer, payload_len);

    return static_cast<int>(total_frame_bytes);
}

} // namespace audio_codecs::aac
