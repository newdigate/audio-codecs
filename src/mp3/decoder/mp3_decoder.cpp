#include "audio_codecs/mp3/mp3_decoder.h"
#include "src/mp3/mp3_common.h"
#include "src/mp3/decoder/bit_reservoir.h"
#include "src/mp3/decoder/huffman_decoder.h"
#include "src/mp3/decoder/requantizer.h"
#include "src/mp3/decoder/imdct.h"
#include "src/mp3/decoder/synthesis_filter.h"
#include <new>
#include <cstring>
#include <algorithm>

namespace audio_codecs::mp3 {

struct Mp3Decoder::Impl {
    AudioConfig config;
    FrameHeader last_header;
    bool has_header{false};
    size_t last_sync_offset{0};
    size_t last_frame_bytes{0};

    BitReservoir reservoir;
    ImdctEngine imdct;
    SynthesisFilter synth[2];

    uint8_t scratch_main_data[4096];
    int16_t is[2][576];
    float xr[2][576];
    float subband_time[2][32][18];
    ScalefactorData sf[2];

    void reset() {
        has_header = false;
        last_sync_offset = 0;
        last_frame_bytes = 0;
        reservoir.reset();
        imdct.reset();
        synth[0].reset();
        synth[1].reset();
    }
};

Mp3Decoder::Mp3Decoder() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "Mp3Decoder state_buffer_ too small for Impl");
    impl_ = new (state_buffer_) Impl();
    reset();
}

Mp3Decoder::~Mp3Decoder() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

bool Mp3Decoder::init(const AudioConfig& config) {
    if (!impl_) {
        impl_ = new (state_buffer_) Impl();
    }
    impl_->config = config;
    reset();
    return true;
}

void Mp3Decoder::reset() {
    if (impl_) {
        impl_->reset();
    }
}

bool Mp3Decoder::get_frame_info(uint32_t& sample_rate, uint8_t& channels, uint32_t& bitrate_kbps) const {
    if (!impl_ || !impl_->has_header) return false;
    sample_rate  = impl_->last_header.sample_rate;
    channels     = impl_->last_header.channels;
    bitrate_kbps = impl_->last_header.bitrate_kbps;
    return true;
}

size_t Mp3Decoder::get_last_frame_bytes() const {
    return impl_ ? impl_->last_frame_bytes : 0;
}

size_t Mp3Decoder::get_last_sync_offset() const {
    return impl_ ? impl_->last_sync_offset : 0;
}

int Mp3Decoder::decode_frame(const uint8_t* in_data, size_t in_bytes, 
                             float* out_pcm, size_t max_out_samples) {
    if (!impl_ || !in_data || in_bytes < 4 || !out_pcm) {
        return -1;
    }

    // 1. Find syncword and parse FrameHeader
    size_t sync_offset = 0;
    FrameHeader header;
    bool header_found = false;

    while (sync_offset + 4 <= in_bytes) {
        uint32_t word = (static_cast<uint32_t>(in_data[sync_offset]) << 24) |
                        (static_cast<uint32_t>(in_data[sync_offset + 1]) << 16) |
                        (static_cast<uint32_t>(in_data[sync_offset + 2]) << 8) |
                        static_cast<uint32_t>(in_data[sync_offset + 3]);

        if (parse_frame_header(word, header)) {
            header_found = true;
            break;
        }
        sync_offset++;
    }

    if (!header_found || sync_offset + header.frame_bytes > in_bytes) {
        return -2; // Incomplete or invalid frame
    }

    impl_->last_header = header;
    impl_->has_header = true;
    impl_->last_sync_offset = sync_offset;
    impl_->last_frame_bytes = header.frame_bytes;

    const uint8_t* frame_ptr = in_data + sync_offset;
    size_t header_size = 4;
    if (!header.protection_bit) {
        header_size += 2; // 16-bit CRC
    }

    // 2. Unpack Side Information
    core::BitReader side_reader;
    side_reader.init(frame_ptr + header_size, header.side_info_bytes);

    SideInfo side;
    side.main_data_begin = static_cast<uint16_t>(side_reader.read_bits(header.version == MpegVersion::Mpeg1 ? 9 : 8));
    side.private_bits = static_cast<uint8_t>(side_reader.read_bits(header.channels == 1 ? (header.version == MpegVersion::Mpeg1 ? 5 : 1) :
                                                                                          (header.version == MpegVersion::Mpeg1 ? 3 : 2)));

    if (header.version == MpegVersion::Mpeg1) {
        for (int ch = 0; ch < header.channels; ++ch) {
            for (int band = 0; band < 4; ++band) {
                side.scfsi[ch][band] = static_cast<uint8_t>(side_reader.read_bits(1));
            }
        }
    }

    for (int gr = 0; gr < header.ngr; ++gr) {
        for (int ch = 0; ch < header.channels; ++ch) {
            GranuleChannelInfo& gi = side.gr[gr][ch];
            gi.part2_3_length = static_cast<uint16_t>(side_reader.read_bits(12));
            gi.big_values = static_cast<uint16_t>(side_reader.read_bits(9));
            gi.global_gain = static_cast<uint8_t>(side_reader.read_bits(8));
            gi.scalefac_compress = static_cast<uint16_t>(side_reader.read_bits(header.version == MpegVersion::Mpeg1 ? 4 : 9));
            gi.window_switching_flag = (side_reader.read_bits(1) != 0);

            if (gi.window_switching_flag) {
                gi.block_type = static_cast<uint8_t>(side_reader.read_bits(2));
                gi.mixed_block_flag = (side_reader.read_bits(1) != 0);
                for (int region = 0; region < 2; ++region) {
                    gi.table_select[region] = static_cast<uint8_t>(side_reader.read_bits(5));
                }
                gi.table_select[2] = 0;
                for (int window = 0; window < 3; ++window) {
                    gi.subblock_gain[window] = static_cast<uint8_t>(side_reader.read_bits(3));
                }
            } else {
                gi.block_type = 0;
                gi.mixed_block_flag = false;
                for (int region = 0; region < 3; ++region) {
                    gi.table_select[region] = static_cast<uint8_t>(side_reader.read_bits(5));
                }
                gi.region0_count = static_cast<uint8_t>(side_reader.read_bits(4));
                gi.region1_count = static_cast<uint8_t>(side_reader.read_bits(3));
            }

            if (header.version == MpegVersion::Mpeg1) {
                gi.preflag = (side_reader.read_bits(1) != 0);
            }
            gi.scalefac_scale = (side_reader.read_bits(1) != 0);
            gi.count1table_select = (side_reader.read_bits(1) != 0);
        }
    }

    // 3. Append main data to BitReservoir
    size_t static_headers_len = header_size + header.side_info_bytes;
    size_t current_frame_main_bytes = (header.frame_bytes > static_headers_len) ? (header.frame_bytes - static_headers_len) : 0;
    impl_->reservoir.append_main_data(frame_ptr + static_headers_len, current_frame_main_bytes);

    // 4. Calculate total main data bits needed across granules
    size_t total_part2_3_bits = 0;
    for (int gr = 0; gr < header.ngr; ++gr) {
        for (int ch = 0; ch < header.channels; ++ch) {
            total_part2_3_bits += side.gr[gr][ch].part2_3_length;
        }
    }
    size_t total_main_bytes = (total_part2_3_bits + 7) >> 3;

    core::BitReader main_reader;
    if (!impl_->reservoir.prepare_reader(side.main_data_begin, current_frame_main_bytes, total_main_bytes + 64, main_reader, impl_->scratch_main_data)) {
        return -3; // Bit reservoir underflow
    }

    size_t total_pcm_samples = header.ngr * 576 * header.channels;
    if (max_out_samples < total_pcm_samples) {
        return -4; // Output buffer too small
    }

    size_t out_pcm_idx = 0;

    // 5. Decode Granules
    for (int gr = 0; gr < header.ngr; ++gr) {
        for (int ch = 0; ch < header.channels; ++ch) {
            GranuleChannelInfo& gi = side.gr[gr][ch];
            size_t part2_bits_read = 0;

            // A. Scalefactors
            Requantizer::decode_scalefactors(main_reader, header, side, gr, ch, impl_->sf[ch], part2_bits_read);

            // B. Huffman Decoding
            size_t part3_bits = (gi.part2_3_length > part2_bits_read) ? (gi.part2_3_length - part2_bits_read) : 0;
            HuffmanDecoder::decode_granule(main_reader, gi, header, impl_->is[ch], part3_bits);

            // C. Requantization
            Requantizer::requantize_granule(impl_->is[ch], impl_->sf[ch], gi, header, impl_->xr[ch]);

            // D. Reorder Short Blocks
            if (gi.window_switching_flag && (gi.block_type == 2)) {
                Requantizer::reorder_short_blocks(impl_->xr[ch], header);
            }
        }

        // E. Stereo Processing
        if (header.channels == 2) {
            Requantizer::process_stereo(impl_->xr[0], impl_->xr[1], side.gr[gr][0], side.gr[gr][1], header);
        }

        // F. Alias Reduction, IMDCT, and Synthesis Filterbank
        for (int ch = 0; ch < header.channels; ++ch) {
            GranuleChannelInfo& gi = side.gr[gr][ch];
            Requantizer::alias_reduction(impl_->xr[ch], gi);

            // Transform each of the 32 subbands with 18-point IMDCT
            for (int sb = 0; sb < 32; ++sb) {
                const float* xr_sb = &impl_->xr[ch][sb * 18];
                float out_18[18];
                impl_->imdct.transform_subband(xr_sb, ch, sb, out_18, gi.block_type);

                for (int t = 0; t < 18; ++t) {
                    float val = out_18[t];
                    // Frequency inversion compensation for odd subband & odd time step
                    if ((sb & 1) && (t & 1)) {
                        val = -val;
                    }
                    impl_->subband_time[ch][sb][t] = val;
                }
            }
        }

        // G. Polyphase Synthesis Filterbank across 18 time steps
        for (int t = 0; t < 18; ++t) {
            float pcm_ch[2][32];

            for (int ch = 0; ch < header.channels; ++ch) {
                float s_32[32];
                for (int sb = 0; sb < 32; ++sb) {
                    s_32[sb] = impl_->subband_time[ch][sb][t];
                }
                impl_->synth[ch].filter_subband(s_32, pcm_ch[ch]);
            }

            // Interleave PCM samples
            for (int j = 0; j < 32; ++j) {
                for (int ch = 0; ch < header.channels; ++ch) {
                    out_pcm[out_pcm_idx++] = pcm_ch[ch][j];
                }
            }
        }
    }

    return static_cast<int>(out_pcm_idx);
}

} // namespace audio_codecs::mp3
