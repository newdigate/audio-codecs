#include "include/audio_codecs/aac/aac_decoder.h"
#include "include/audio_codecs/aac/adts_header.h"
#include "src/aac/adts_parser.h"
#include "src/aac/aac_tables.h"
#include "src/aac/aac_mdct.h"
#include "src/aac/decoder/huffman_decoder.h"
#include "src/aac/decoder/requantizer.h"
#include "src/aac/decoder/stereo_processor.h"
#include "src/aac/decoder/tns_decoder.h"
#include "src/core/bit_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <new>

namespace audio_codecs::aac {

namespace {

inline void byte_align(core::BitReader& reader) {
    size_t rem = reader.get_position_bits() % 8;
    if (rem != 0) {
        reader.skip_bits(static_cast<int>(8 - rem));
    }
}

struct IcsInfo {
    WindowSequence window_sequence{WindowSequence::OnlyLong};
    WindowShape window_shape{WindowShape::Sine};
    uint8_t max_sfb{0};
    uint8_t scale_factor_grouping{0};
    uint8_t num_window_groups{1};
    uint8_t window_group_length[8]{1, 0, 0, 0, 0, 0, 0, 0};
    uint8_t num_windows{1};
    bool predictor_data_present{false};
};

struct ChannelStream {
    uint8_t global_gain{0};
    IcsInfo ics;
    uint8_t sfb_cb[8][AAC_MAX_SCALEFACTOR_BANDS]{{0}};
    int scalefactors[8][AAC_MAX_SCALEFACTOR_BANDS]{{0}};
    bool pulse_data_present{false};
    bool tns_data_present{false};
    TnsData tns;
    bool gain_control_data_present{false};

    // Quantized spectral coefficients
    int quant[1024]{0};

    // De-grouped buffers for EightShort
    int degrouped_quant[1024]{0};
    int degrouped_sf[8 * AAC_MAX_SCALEFACTOR_BANDS]{0};
    uint8_t degrouped_cb[8 * AAC_MAX_SCALEFACTOR_BANDS]{0};
    uint8_t degrouped_is_type[8 * AAC_MAX_SCALEFACTOR_BANDS]{0};
    uint8_t degrouped_pns_active[8 * AAC_MAX_SCALEFACTOR_BANDS]{0};

    // Long window helpers
    uint8_t is_type[AAC_MAX_SCALEFACTOR_BANDS]{0};
    uint8_t pns_active[AAC_MAX_SCALEFACTOR_BANDS]{0};

    // Requantized float spectrum
    float float_spec[1024]{0.0f};
};

bool parse_ics_info(core::BitReader& reader, IcsInfo& ics) {
    if (reader.bits_remaining() < 10) return false;
    reader.read_bits(1); // ics_reserved_bit

    uint32_t seq_val = reader.read_bits(2);
    ics.window_sequence = static_cast<WindowSequence>(seq_val);
    ics.window_shape = (reader.read_bits(1) != 0) ? WindowShape::KBD : WindowShape::Sine;

    if (ics.window_sequence == WindowSequence::EightShort) {
        ics.max_sfb = static_cast<uint8_t>(reader.read_bits(4));
        ics.scale_factor_grouping = static_cast<uint8_t>(reader.read_bits(7));
        ics.num_windows = 8;

        // Calculate window grouping
        ics.num_window_groups = 1;
        ics.window_group_length[0] = 1;
        for (int i = 0; i < 7; ++i) {
            int bit = (ics.scale_factor_grouping >> (6 - i)) & 1;
            if (bit == 1) {
                ics.window_group_length[ics.num_window_groups - 1]++;
            } else {
                ics.num_window_groups++;
                ics.window_group_length[ics.num_window_groups - 1] = 1;
            }
        }
    } else {
        ics.max_sfb = static_cast<uint8_t>(reader.read_bits(6));
        ics.num_windows = 1;
        ics.num_window_groups = 1;
        ics.window_group_length[0] = 1;
        ics.scale_factor_grouping = 0;

        ics.predictor_data_present = (reader.read_bits(1) != 0);
        if (ics.predictor_data_present) {
            bool predictor_reset = (reader.read_bits(1) != 0);
            if (predictor_reset) {
                reader.read_bits(5); // predictor_reset_group_number
            }
            uint8_t pred_sfb_max = (ics.max_sfb < 30) ? ics.max_sfb : 30;
            for (uint8_t b = 0; b < pred_sfb_max; ++b) {
                reader.read_bits(1); // prediction_used[b]
            }
        }
    }
    return true;
}

bool decode_ics(core::BitReader& reader,
                bool common_window,
                const IcsInfo& shared_ics,
                ChannelStream& ch,
                const int* swb_offsets_long,
                size_t num_swb_long,
                const int* swb_offsets_short,
                size_t num_swb_short) {
    std::memset(&ch, 0, sizeof(ChannelStream));
    if (reader.bits_remaining() < 8) return false;
    ch.global_gain = static_cast<uint8_t>(reader.read_bits(8));

    if (common_window) {
        ch.ics = shared_ics;
    } else {
        if (!parse_ics_info(reader, ch.ics)) {
            return false;
        }
    }

    const IcsInfo& ics = ch.ics;
    bool is_short = (ics.window_sequence == WindowSequence::EightShort);
    const int* swb_offsets = is_short ? swb_offsets_short : swb_offsets_long;
    size_t num_swb = is_short ? num_swb_short : num_swb_long;

    // 1. Section Data
    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        uint8_t sfb = 0;
        while (sfb < ics.max_sfb) {
            if (reader.bits_remaining() < 4) return false;
            uint8_t sect_cb = static_cast<uint8_t>(reader.read_bits(4));
            uint8_t sect_len = 0;

            if (ics.window_sequence == WindowSequence::EightShort) {
                if (reader.bits_remaining() < 3) return false;
                uint8_t incr = static_cast<uint8_t>(reader.read_bits(3));
                while (incr == 7) {
                    sect_len += 7;
                    if (reader.bits_remaining() < 3) return false;
                    incr = static_cast<uint8_t>(reader.read_bits(3));
                }
                sect_len += incr;
            } else {
                if (reader.bits_remaining() < 5) return false;
                uint8_t incr = static_cast<uint8_t>(reader.read_bits(5));
                while (incr == 31) {
                    sect_len += 31;
                    if (reader.bits_remaining() < 5) return false;
                    incr = static_cast<uint8_t>(reader.read_bits(5));
                }
                sect_len += incr;
            }

            if (sect_len == 0 || sfb + sect_len > num_swb) {
                if (sfb + sect_len > num_swb) {
                    sect_len = static_cast<uint8_t>(num_swb - sfb);
                }
            }

            for (uint8_t b = sfb; b < sfb + sect_len && b < num_swb; ++b) {
                ch.sfb_cb[g][b] = sect_cb;
            }
            sfb += sect_len;
        }

        for (size_t b = ics.max_sfb; b < num_swb; ++b) {
            ch.sfb_cb[g][b] = HCB_ZERO;
        }
    }

    // 2. Scale Factor Data
    int last_sf = ch.global_gain;
    int last_is = 0;
    int last_noise = ch.global_gain - 90;
    bool noise_pns_first = true;

    for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
        for (uint8_t sfb = 0; sfb < ics.max_sfb && sfb < num_swb; ++sfb) {
            uint8_t cb = ch.sfb_cb[g][sfb];
            if (cb == HCB_ZERO) {
                ch.scalefactors[g][sfb] = 0;
            } else if (cb == HCB_INTENSITY || cb == HCB_INTENSITY2) {
                int delta = 0;
                if (!decode_scalefactor_delta(reader, delta)) return false;
                last_is += delta;
                ch.scalefactors[g][sfb] = last_is;
            } else if (cb == HCB_NOISE) {
                if (noise_pns_first) {
                    if (reader.bits_remaining() < 9) return false;
                    int pns_pcm = static_cast<int>(reader.read_bits(9));
                    last_noise = pns_pcm - 256 + ch.global_gain - 90;
                    noise_pns_first = false;
                } else {
                    int delta = 0;
                    if (!decode_scalefactor_delta(reader, delta)) return false;
                    last_noise += delta;
                }
                ch.scalefactors[g][sfb] = last_noise;
            } else {
                int delta = 0;
                if (!decode_scalefactor_delta(reader, delta)) return false;
                last_sf += delta;
                ch.scalefactors[g][sfb] = last_sf;
            }
        }
    }

    // 3. Pulse Data
    if (reader.bits_remaining() < 1) return false;
    ch.pulse_data_present = (reader.read_bits(1) != 0);
    if (ch.pulse_data_present) {
        if (ics.window_sequence == WindowSequence::EightShort) {
            return false;
        }
        if (reader.bits_remaining() < 8) return false;
        uint8_t number_pulse = static_cast<uint8_t>(reader.read_bits(2));
        reader.read_bits(6); // pulse_start_sfb
        for (int p = 0; p <= number_pulse; ++p) {
            if (reader.bits_remaining() < 9) return false;
            reader.read_bits(5); // pulse_offset
            reader.read_bits(4); // pulse_amp
        }
    }

    // 4. TNS Data
    if (reader.bits_remaining() < 1) return false;
    ch.tns_data_present = (reader.read_bits(1) != 0);
    if (ch.tns_data_present) {
        bool is_short = (ics.window_sequence == WindowSequence::EightShort);
        for (uint8_t w = 0; w < ics.num_windows; ++w) {
            if (reader.bits_remaining() < (is_short ? 1 : 2)) return false;
            uint8_t n_filt = static_cast<uint8_t>(reader.read_bits(is_short ? 1 : 2));
            ch.tns.n_filt[w] = n_filt;
            if (n_filt > 0) {
                if (reader.bits_remaining() < 1) return false;
                uint8_t coef_res = static_cast<uint8_t>(reader.read_bits(1));
                for (uint8_t f = 0; f < n_filt; ++f) {
                    if (reader.bits_remaining() < (is_short ? 11 : 17)) return false;
                    uint8_t length = static_cast<uint8_t>(reader.read_bits(is_short ? 4 : 6));
                    uint8_t order = static_cast<uint8_t>(reader.read_bits(is_short ? 3 : 5));

                    TnsFilter& filt = ch.tns.filters[w][f];
                    filt.start_band = 0;
                    filt.stop_band = length;
                    filt.order = order;

                    if (order > 0) {
                        filt.direction = static_cast<uint8_t>(reader.read_bits(1));
                        uint8_t coef_compress = static_cast<uint8_t>(reader.read_bits(1));
                        int coef_bits = (coef_res ? 4 : 3) - (coef_compress ? 1 : 0);
                        int raw_coef[16] = {0};
                        for (int k = 0; k < order; ++k) {
                            if (reader.bits_remaining() < static_cast<size_t>(coef_bits)) return false;
                            raw_coef[k] = static_cast<int>(reader.read_bits(coef_bits));
                        }
                        decode_tns_coef(order, coef_res ? 4 : 3, raw_coef, filt.coef);
                    }
                }
            }
        }
    }

    // 5. Gain Control Data
    if (reader.bits_remaining() < 1) return false;
    ch.gain_control_data_present = (reader.read_bits(1) != 0);
    if (ch.gain_control_data_present) {
        return false;
    }

    // 6. Spectral Data
    if (ics.window_sequence == WindowSequence::EightShort) {
        int grouped_spec[8][128]{{0}};

        for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
            uint8_t group_len = ics.window_group_length[g];
            for (uint8_t sfb = 0; sfb < ics.max_sfb && sfb < num_swb; ++sfb) {
                uint8_t cb = ch.sfb_cb[g][sfb];
                int start = swb_offsets[sfb];
                int end = swb_offsets[sfb + 1];
                int width = end - start;
                int total_lines = width * group_len;

                if (cb == HCB_ZERO || cb == HCB_NOISE || cb == HCB_INTENSITY || cb == HCB_INTENSITY2) {
                    // Lines remain 0
                } else {
                    int temp_lines[1024];
                    if (!decode_spectral_data(reader, cb, temp_lines, total_lines)) {
                        return false;
                    }
                    for (int w = 0; w < group_len; ++w) {
                        for (int i = 0; i < width; ++i) {
                            grouped_spec[g][w * 128 + start + i] = temp_lines[w * width + i];
                        }
                    }
                }
            }
        }

        // De-grouping to 8 consecutive 128-line windows
        uint8_t abs_w = 0;
        for (uint8_t g = 0; g < ics.num_window_groups; ++g) {
            uint8_t group_len = ics.window_group_length[g];
            for (uint8_t rel_w = 0; rel_w < group_len; ++rel_w) {
                uint8_t w = abs_w + rel_w;
                for (size_t sfb = 0; sfb < num_swb; ++sfb) {
                    size_t idx = w * num_swb + sfb;
                    ch.degrouped_sf[idx] = ch.scalefactors[g][sfb];
                    uint8_t cb = ch.sfb_cb[g][sfb];
                    ch.degrouped_cb[idx] = cb;
                    if (cb == HCB_INTENSITY) {
                        ch.degrouped_is_type[idx] = 1;
                    } else if (cb == HCB_INTENSITY2) {
                        ch.degrouped_is_type[idx] = 2;
                    } else {
                        ch.degrouped_is_type[idx] = 0;
                    }
                    ch.degrouped_pns_active[idx] = (cb == HCB_NOISE) ? 1 : 0;

                    int start = swb_offsets[sfb];
                    int end = swb_offsets[sfb + 1];
                    for (int i = start; i < end; ++i) {
                        ch.degrouped_quant[w * 128 + i] = grouped_spec[g][rel_w * 128 + i];
                    }
                }
            }
            abs_w += group_len;
        }
    } else {
        // Long window
        for (uint8_t sfb = 0; sfb < ics.max_sfb && sfb < num_swb; ++sfb) {
            uint8_t cb = ch.sfb_cb[0][sfb];
            int start = swb_offsets[sfb];
            int end = swb_offsets[sfb + 1];
            int width = end - start;

            if (cb == HCB_INTENSITY) {
                ch.is_type[sfb] = 1;
            } else if (cb == HCB_INTENSITY2) {
                ch.is_type[sfb] = 2;
            } else {
                ch.is_type[sfb] = 0;
            }
            ch.pns_active[sfb] = (cb == HCB_NOISE) ? 1 : 0;

            if (cb >= 1 && cb <= 11) {
                if (!decode_spectral_data(reader, cb, &ch.quant[start], width)) {
                    return false;
                }
            }
        }
    }

    return true;
}

} // anonymous namespace

struct AacDecoder::Impl {
    AudioConfig config;
    AdtsHeader last_header;
    bool has_header{false};
    size_t last_sync_offset{0};
    size_t last_frame_bytes{0};

    AacMdct mdct;
    float overlap[2][1024]{{0.0f}};
    WindowSequence last_window_seq[2]{WindowSequence::OnlyLong, WindowSequence::OnlyLong};
    WindowShape last_window_shape[2]{WindowShape::Sine, WindowShape::Sine};
    uint32_t rng_state{123456789};

    // Frame decode scratch buffers
    ChannelStream ch[2];
    float pcm_buf[2][1024]{{0.0f}};
    float time_buf[2048]{0.0f};

    void reset() {
        has_header = false;
        last_sync_offset = 0;
        last_frame_bytes = 0;
        std::memset(overlap, 0, sizeof(overlap));
        last_window_seq[0] = WindowSequence::OnlyLong;
        last_window_seq[1] = WindowSequence::OnlyLong;
        last_window_shape[0] = WindowShape::Sine;
        last_window_shape[1] = WindowShape::Sine;
        rng_state = 123456789;
    }
};

AacDecoder::AacDecoder() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "AacDecoder state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
    reset();
}

AacDecoder::~AacDecoder() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

bool AacDecoder::init(const AudioConfig& config) {
    if (!impl_) {
        impl_ = new (state_buffer_) Impl();
    }
    impl_->config = config;
    reset();
    return true;
}

void AacDecoder::reset() {
    if (impl_) {
        impl_->reset();
    }
}

bool AacDecoder::get_frame_info(uint32_t& sample_rate, uint8_t& channels, uint32_t& bitrate_kbps) const {
    if (!impl_) return false;
    if (impl_->has_header) {
        sample_rate = impl_->last_header.sample_rate;
        channels = impl_->last_header.channel_configuration;
        bitrate_kbps = impl_->config.bitrate_kbps;
        return true;
    } else if (impl_->config.sample_rate > 0) {
        sample_rate = impl_->config.sample_rate;
        channels = impl_->config.channels;
        bitrate_kbps = impl_->config.bitrate_kbps;
        return true;
    }
    return false;
}

size_t AacDecoder::get_last_frame_bytes() const {
    return impl_ ? impl_->last_frame_bytes : 0;
}

size_t AacDecoder::get_last_sync_offset() const {
    return impl_ ? impl_->last_sync_offset : 0;
}

int AacDecoder::decode_frame(const uint8_t* in_data, size_t in_bytes, 
                             float* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || in_bytes < 3 || !out_pcm) {
        return -1;
    }

    size_t sync_offset = 0;
    bool is_adts = false;
    AdtsHeader header;

    if (find_adts_sync(in_data, in_bytes, sync_offset)) {
        core::BitReader adts_reader;
        adts_reader.init(in_data + sync_offset, in_bytes - sync_offset);
        if (parse_adts_header(adts_reader, header)) {
            if (sync_offset + header.frame_length <= in_bytes) {
                // Verify CRC if present
                if (!header.protection_absent) {
                    uint16_t calculated_crc = calculate_adts_crc(in_data + sync_offset, header.frame_length);
                    if (calculated_crc != header.crc) {
                        return -2; // CRC error
                    }
                }
                is_adts = true;
                impl_->last_header = header;
                impl_->has_header = true;
                impl_->last_sync_offset = sync_offset;
                impl_->last_frame_bytes = header.frame_length;
            }
        }
    }

    const uint8_t* payload_ptr = nullptr;
    size_t payload_bytes = 0;
    uint32_t sample_rate = impl_->config.sample_rate;
    uint8_t target_channels = impl_->config.channels;

    if (is_adts) {
        size_t header_len = header.header_size_bytes();
        payload_ptr = in_data + sync_offset + header_len;
        payload_bytes = (header.frame_length > header_len) ? (header.frame_length - header_len) : 0;
        sample_rate = header.sample_rate;
        target_channels = (header.channel_configuration > 0) ? header.channel_configuration : 2;
    } else {
        payload_ptr = in_data;
        payload_bytes = in_bytes;
        impl_->last_sync_offset = 0;
        impl_->last_frame_bytes = in_bytes;
    }

    int sf_index = get_sampling_frequency_index(sample_rate);
    if (sf_index < 0) {
        sf_index = 4; // default 44.1kHz
    }

    size_t num_swb_long = 0;
    const int* swb_offsets_long = get_swb_offset_long_index(sf_index, num_swb_long);
    size_t num_swb_short = 0;
    const int* swb_offsets_short = get_swb_offset_short_index(sf_index, num_swb_short);

    if (!swb_offsets_long || !swb_offsets_short) {
        return -3;
    }

    core::BitReader reader;
    reader.init(payload_ptr, payload_bytes);

    uint8_t decoded_channels = 0;
    std::memset(impl_->pcm_buf, 0, sizeof(impl_->pcm_buf));

    while (reader.bits_remaining() >= 3 && decoded_channels < target_channels) {
        uint32_t id_val = reader.read_bits(3);
        ElementId id = static_cast<ElementId>(id_val);

        if (id == ElementId::END) {
            break;
        } else if (id == ElementId::SCE || id == ElementId::LFE) {
            reader.read_bits(4); // element_instance_tag
            IcsInfo dummy_shared;
            uint8_t ch_idx = decoded_channels;

            // Peek at window sequence to select swb_offsets table
            // We can decode ICS directly
            if (!decode_ics(reader, false, dummy_shared, impl_->ch[ch_idx], 
                            swb_offsets_long, num_swb_long,
                            swb_offsets_short, num_swb_short)) {
                return -4;
            }

            ChannelStream& ch_stream = impl_->ch[ch_idx];
            bool is_short = (ch_stream.ics.window_sequence == WindowSequence::EightShort);
            const int* swb = is_short ? swb_offsets_short : swb_offsets_long;
            size_t num_swb = is_short ? num_swb_short : num_swb_long;

            // Requantization
            if (is_short) {
                requantize_short_spectrum(ch_stream.degrouped_quant, ch_stream.degrouped_sf,
                                          swb, num_swb, 8, ch_stream.float_spec);
                apply_pns_short(ch_stream.float_spec, ch_stream.degrouped_pns_active,
                                ch_stream.degrouped_sf, swb, num_swb, 8, impl_->rng_state);
            } else {
                requantize_spectrum(ch_stream.quant, ch_stream.scalefactors[0],
                                    swb, num_swb, ch_stream.float_spec);
                apply_pns(ch_stream.float_spec, ch_stream.pns_active,
                          ch_stream.scalefactors[0], swb, num_swb, impl_->rng_state);
            }

            // TNS
            if (ch_stream.tns_data_present) {
                apply_tns(ch_stream.float_spec, ch_stream.tns, swb, num_swb, ch_stream.ics.window_sequence);
            }

            // IMDCT and overlap-add
            impl_->mdct.inverse_windowed(ch_stream.float_spec, impl_->time_buf,
                                         ch_stream.ics.window_sequence,
                                         impl_->last_window_shape[ch_idx],
                                         ch_stream.ics.window_shape);

            for (size_t i = 0; i < 1024; ++i) {
                impl_->pcm_buf[ch_idx][i] = impl_->time_buf[i] + impl_->overlap[ch_idx][i];
                impl_->overlap[ch_idx][i] = impl_->time_buf[1024 + i];
            }
            impl_->last_window_seq[ch_idx] = ch_stream.ics.window_sequence;
            impl_->last_window_shape[ch_idx] = ch_stream.ics.window_shape;

            decoded_channels += 1;

        } else if (id == ElementId::CPE) {
            reader.read_bits(4); // element_instance_tag
            bool common_window = (reader.read_bits(1) != 0);
            IcsInfo shared_ics;
            uint8_t ms_mask_present = 0;
            uint8_t ms_used[8 * AAC_MAX_SCALEFACTOR_BANDS] = {0};
            bool ms_active = false;

            if (common_window) {
                if (!parse_ics_info(reader, shared_ics)) {
                    return -5;
                }
                ms_mask_present = static_cast<uint8_t>(reader.read_bits(2));
                if (ms_mask_present == 1) {
                    ms_active = true;
                    for (uint8_t g = 0; g < shared_ics.num_window_groups; ++g) {
                        for (uint8_t sfb = 0; sfb < shared_ics.max_sfb; ++sfb) {
                            if (reader.bits_remaining() < 1) return -5;
                            ms_used[g * num_swb_long + sfb] = static_cast<uint8_t>(reader.read_bits(1));
                        }
                    }
                } else if (ms_mask_present == 2) {
                    ms_active = true;
                    for (uint8_t g = 0; g < shared_ics.num_window_groups; ++g) {
                        for (uint8_t sfb = 0; sfb < shared_ics.max_sfb; ++sfb) {
                            ms_used[g * num_swb_long + sfb] = 1;
                        }
                    }
                }
            }

            if (!decode_ics(reader, common_window, shared_ics, impl_->ch[0], 
                            swb_offsets_long, num_swb_long,
                            swb_offsets_short, num_swb_short)) {
                return -6;
            }
            if (!decode_ics(reader, common_window, shared_ics, impl_->ch[1], 
                            swb_offsets_long, num_swb_long,
                            swb_offsets_short, num_swb_short)) {
                return -6;
            }

            bool is_short = (impl_->ch[0].ics.window_sequence == WindowSequence::EightShort);
            const int* swb = is_short ? swb_offsets_short : swb_offsets_long;
            size_t num_swb = is_short ? num_swb_short : num_swb_long;

            // Requantization
            if (is_short) {
                requantize_short_spectrum(impl_->ch[0].degrouped_quant, impl_->ch[0].degrouped_sf,
                                          swb, num_swb, 8, impl_->ch[0].float_spec);
                requantize_short_spectrum(impl_->ch[1].degrouped_quant, impl_->ch[1].degrouped_sf,
                                          swb, num_swb, 8, impl_->ch[1].float_spec);

                // M/S Stereo
                if (ms_active) {
                    uint8_t degrouped_ms[8 * AAC_MAX_SCALEFACTOR_BANDS] = {0};
                    uint8_t abs_w = 0;
                    for (uint8_t g = 0; g < shared_ics.num_window_groups; ++g) {
                        for (uint8_t rel_w = 0; rel_w < shared_ics.window_group_length[g]; ++rel_w) {
                            uint8_t w = abs_w + rel_w;
                            for (size_t sfb = 0; sfb < num_swb; ++sfb) {
                                degrouped_ms[w * num_swb + sfb] = ms_used[g * num_swb_long + sfb];
                            }
                        }
                        abs_w += shared_ics.window_group_length[g];
                    }
                    apply_ms_stereo_short(impl_->ch[0].float_spec, impl_->ch[1].float_spec,
                                         degrouped_ms, swb, num_swb, 8);
                }

                // Intensity Stereo
                apply_intensity_stereo_short(impl_->ch[0].float_spec, impl_->ch[1].float_spec,
                                            impl_->ch[1].degrouped_sf, impl_->ch[1].degrouped_is_type,
                                            swb, num_swb, 8);

                // PNS
                apply_pns_short(impl_->ch[0].float_spec, impl_->ch[0].degrouped_pns_active,
                                impl_->ch[0].degrouped_sf, swb, num_swb, 8, impl_->rng_state);
                apply_pns_short(impl_->ch[1].float_spec, impl_->ch[1].degrouped_pns_active,
                                impl_->ch[1].degrouped_sf, swb, num_swb, 8, impl_->rng_state);
            } else {
                requantize_spectrum(impl_->ch[0].quant, impl_->ch[0].scalefactors[0],
                                    swb, num_swb, impl_->ch[0].float_spec);
                requantize_spectrum(impl_->ch[1].quant, impl_->ch[1].scalefactors[0],
                                    swb, num_swb, impl_->ch[1].float_spec);

                // M/S Stereo
                if (ms_active) {
                    apply_ms_stereo(impl_->ch[0].float_spec, impl_->ch[1].float_spec,
                                    ms_used, swb, num_swb);
                }

                // Intensity Stereo
                apply_intensity_stereo(impl_->ch[0].float_spec, impl_->ch[1].float_spec,
                                       impl_->ch[1].scalefactors[0], impl_->ch[1].is_type,
                                       swb, num_swb);

                // PNS
                apply_pns(impl_->ch[0].float_spec, impl_->ch[0].pns_active,
                          impl_->ch[0].scalefactors[0], swb, num_swb, impl_->rng_state);
                apply_pns(impl_->ch[1].float_spec, impl_->ch[1].pns_active,
                          impl_->ch[1].scalefactors[0], swb, num_swb, impl_->rng_state);
            }

            // TNS
            if (impl_->ch[0].tns_data_present) {
                apply_tns(impl_->ch[0].float_spec, impl_->ch[0].tns, swb, num_swb, impl_->ch[0].ics.window_sequence);
            }
            if (impl_->ch[1].tns_data_present) {
                apply_tns(impl_->ch[1].float_spec, impl_->ch[1].tns, swb, num_swb, impl_->ch[1].ics.window_sequence);
            }

            // IMDCT and Overlap-Add for both channels
            for (int c = 0; c < 2; ++c) {
                impl_->mdct.inverse_windowed(impl_->ch[c].float_spec, impl_->time_buf,
                                             impl_->ch[c].ics.window_sequence,
                                             impl_->last_window_shape[c],
                                             impl_->ch[c].ics.window_shape);

                for (size_t i = 0; i < 1024; ++i) {
                    impl_->pcm_buf[c][i] = impl_->time_buf[i] + impl_->overlap[c][i];
                    impl_->overlap[c][i] = impl_->time_buf[1024 + i];
                }
                impl_->last_window_seq[c] = impl_->ch[c].ics.window_sequence;
                impl_->last_window_shape[c] = impl_->ch[c].ics.window_shape;
            }

            decoded_channels += 2;

        } else if (id == ElementId::DSE) {
            reader.read_bits(4); // element_instance_tag
            bool align_flag = (reader.read_bits(1) != 0);
            uint32_t count = reader.read_bits(8);
            if (count == 255) {
                uint32_t esc = reader.read_bits(8);
                count += esc;
            }
            if (align_flag) {
                byte_align(reader);
            }
            for (uint32_t i = 0; i < count; ++i) {
                reader.read_bits(8);
            }
        } else if (id == ElementId::PCE) {
            // Program config element: skip instance tag and read basic header
            reader.read_bits(4); // element_instance_tag
            reader.read_bits(2); // profile
            reader.read_bits(4); // sf_index
            uint32_t num_front = reader.read_bits(4);
            uint32_t num_side = reader.read_bits(4);
            uint32_t num_back = reader.read_bits(4);
            uint32_t num_lfe = reader.read_bits(2);
            uint32_t num_assoc = reader.read_bits(3);
            uint32_t num_cc = reader.read_bits(4);
            if (reader.read_bits(1)) reader.read_bits(4); // mono mixdown
            if (reader.read_bits(1)) reader.read_bits(4); // stereo mixdown
            if (reader.read_bits(1)) reader.read_bits(3); // matrix mixdown
            for (uint32_t i = 0; i < num_front + num_side + num_back; ++i) reader.read_bits(5);
            for (uint32_t i = 0; i < num_lfe; ++i) reader.read_bits(4);
            for (uint32_t i = 0; i < num_assoc; ++i) reader.read_bits(4);
            for (uint32_t i = 0; i < num_cc; ++i) reader.read_bits(5);
            byte_align(reader);
            uint32_t comment_bytes = reader.read_bits(8);
            for (uint32_t i = 0; i < comment_bytes; ++i) reader.read_bits(8);
        } else if (id == ElementId::FIL) {
            uint32_t count = reader.read_bits(4);
            if (count == 15) {
                count += reader.read_bits(8) - 1;
            }
            for (uint32_t i = 0; i < count; ++i) {
                reader.read_bits(8);
            }
        } else {
            // Unknown or unsupported element
            break;
        }
    }

    if (decoded_channels == 0) {
        // Fallback for silent/empty frame: output overlap buffer
        decoded_channels = target_channels;
        for (uint8_t c = 0; c < decoded_channels && c < 2; ++c) {
            for (size_t i = 0; i < 1024; ++i) {
                impl_->pcm_buf[c][i] = impl_->overlap[c][i];
                impl_->overlap[c][i] = 0.0f;
            }
        }
    }

    size_t total_samples = static_cast<size_t>(decoded_channels) * 1024;
    if (max_out_samples < total_samples) {
        return -7; // Output buffer too small
    }

    // Interleave output PCM
    size_t out_idx = 0;
    for (size_t i = 0; i < 1024; ++i) {
        for (uint8_t c = 0; c < decoded_channels; ++c) {
            out_pcm[out_idx++] = impl_->pcm_buf[c][i];
        }
    }

    return static_cast<int>(total_samples);
}

} // namespace audio_codecs::aac
