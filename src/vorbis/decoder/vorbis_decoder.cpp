#include "audio_codecs/vorbis/vorbis_decoder.h"
#include "src/vorbis/decoder/header_parser.h"
#include "src/vorbis/vorbis_mdct.h"
#include "src/vorbis/vorbis_common.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace audio_codecs::vorbis {

struct VorbisDecoder::Impl {
    ogg::OggDemuxer demuxer;
    VorbisInfo info{};
    VorbisComment comment{};
    VorbisSetup setup{};

    VorbisMdct mdct_short;
    VorbisMdct mdct_long;

    bool id_parsed{false};
    bool comment_parsed{false};
    bool setup_parsed{false};
    bool initialized{false};

    size_t prev_blocksize{0};
    size_t overlap_len{0};
    alignas(16) float overlap_buffer[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE / 2]{{0}};

    // Scratch buffers
    alignas(16) float residue_ch[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE / 2]{{0}};
    alignas(16) float floor_curve[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE / 2]{{0}};
    int32_t floor_y[VORBIS_MAX_CHANNELS][VORBIS_MAX_FLOOR1_POSTS]{{0}};
    bool ch_nonzero[VORBIS_MAX_CHANNELS]{false};

    alignas(16) float imdct_out[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE]{{0}};
    alignas(16) float window[VORBIS_MAX_BLOCK_SIZE]{0};
    alignas(16) uint8_t packet_buffer[65536]{0};

    bool is_ogg_stream{false};

    void reset() {
        demuxer.reset();
        is_ogg_stream = false;
        prev_blocksize = 0;
        overlap_len = 0;
        std::memset(overlap_buffer, 0, sizeof(overlap_buffer));
        if (setup_parsed) {
            mdct_short.init(setup.blocksize_0);
            mdct_long.init(setup.blocksize_1);
            prev_blocksize = setup.blocksize_0;
            overlap_len = setup.blocksize_0 / 2;
        }
    }
};

VorbisDecoder::VorbisDecoder() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for VorbisDecoder::Impl");
    impl_ = new (state_buffer_) Impl();
}

VorbisDecoder::~VorbisDecoder() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

bool VorbisDecoder::init(const AudioConfig& config) {
    if (!impl_) return false;
    impl_->reset();
    impl_->initialized = true;
    return true;
}

void VorbisDecoder::reset() {
    if (impl_) {
        impl_->reset();
    }
}

bool VorbisDecoder::has_headers() const {
    return impl_ && impl_->id_parsed && impl_->setup_parsed;
}

const VorbisInfo& VorbisDecoder::get_info() const {
    return impl_->info;
}

const VorbisComment& VorbisDecoder::get_comment() const {
    return impl_->comment;
}

bool VorbisDecoder::parse_header_packet(const uint8_t* in_packet, size_t in_bytes) {
    if (!impl_ || !in_packet || in_bytes < 7) return false;

    uint8_t ptype = in_packet[0];
    if (ptype == VORBIS_PACKET_ID) {
        if (parse_vorbis_id_header(in_packet, in_bytes, impl_->info)) {
            impl_->id_parsed = true;
            return true;
        }
    } else if (ptype == VORBIS_PACKET_COMMENT) {
        if (parse_vorbis_comment_header(in_packet, in_bytes, impl_->comment)) {
            impl_->comment_parsed = true;
            return true;
        }
    } else if (ptype == VORBIS_PACKET_SETUP) {
        if (impl_->id_parsed && parse_vorbis_setup_header(in_packet, in_bytes, impl_->info.channels, impl_->setup)) {
            impl_->setup_parsed = true;
            impl_->reset();
            return true;
        }
    }
    return false;
}

int VorbisDecoder::decode_packet(const uint8_t* in_packet, size_t in_bytes, 
                                 float* out_pcm, size_t max_out_samples) {
    if (!impl_ || !has_headers() || !in_packet || in_bytes == 0 || !out_pcm) {
        return -1;
    }

    VorbisBitReader reader(in_packet, in_bytes);
    uint32_t packet_type = 0;
    if (!reader.read_bits(1, packet_type) || packet_type != 0) {
        return 0; // Header packet or invalid
    }

    uint32_t mode_bits = vorbis_ilog(impl_->setup.mode_count - 1);
    uint32_t mode_idx = 0;
    if (mode_bits > 0 && !reader.read_bits(mode_bits, mode_idx)) return -1;
    if (mode_idx >= impl_->setup.mode_count) return -1;

    const VorbisMode& mode = impl_->setup.modes[mode_idx];
    size_t blocksize = mode.blockflag ? impl_->setup.blocksize_1 : impl_->setup.blocksize_0;
    size_t n2 = blocksize / 2;

    size_t prev_bs = impl_->prev_blocksize ? impl_->prev_blocksize : blocksize;
    size_t next_bs = blocksize;

    if (mode.blockflag) {
        uint32_t prev_flag = 0, next_flag = 0;
        if (!reader.read_bits(1, prev_flag) || !reader.read_bits(1, next_flag)) return -1;
        prev_bs = prev_flag ? impl_->setup.blocksize_1 : impl_->setup.blocksize_0;
        next_bs = next_flag ? impl_->setup.blocksize_1 : impl_->setup.blocksize_0;
    }

    if (mode.mapping >= impl_->setup.mapping_count) return -1;
    const VorbisMappingConfig& mapping = impl_->setup.mappings[mode.mapping];
    uint8_t channels = impl_->info.channels;

    // 1. Decode Floors
    for (uint8_t c = 0; c < channels; ++c) {
        uint8_t submap = mapping.ch_mux[c];
        uint8_t floor_idx = mapping.submap_floor[submap];
        if (floor_idx >= impl_->setup.floor_count) return -1;

        impl_->ch_nonzero[c] = vorbis_floor1_decode(
            reader, impl_->setup.floors[floor_idx], 
            impl_->setup.codebooks, impl_->setup.codebook_count, 
            impl_->floor_y[c]
        );
    }

    // 2. Decode Residues
    float* res_ptrs[VORBIS_MAX_CHANNELS];
    for (uint8_t c = 0; c < channels; ++c) {
        std::memset(impl_->residue_ch[c], 0, n2 * sizeof(float));
        res_ptrs[c] = impl_->residue_ch[c];
    }

    for (size_t s = 0; s < mapping.submaps; ++s) {
        uint8_t res_idx = mapping.submap_residue[s];
        if (res_idx >= impl_->setup.residue_count) return -1;

        vorbis_residue_decode(
            reader, impl_->setup.residues[res_idx],
            impl_->setup.codebooks, impl_->setup.codebook_count,
            res_ptrs, impl_->ch_nonzero, channels, n2
        );
    }

    // 3. Inverse Polar Channel Coupling
    vorbis_mapping_decouple(mapping, res_ptrs, n2);

    // 4. Dot-product floor with residue to form spectral coefficients
    for (uint8_t c = 0; c < channels; ++c) {
        uint8_t submap = mapping.ch_mux[c];
        uint8_t floor_idx = mapping.submap_floor[submap];

        if (impl_->ch_nonzero[c]) {
            vorbis_floor1_render(impl_->setup.floors[floor_idx], impl_->floor_y[c], impl_->floor_curve[c], n2);
            for (size_t k = 0; k < n2; ++k) {
                impl_->residue_ch[c][k] *= impl_->floor_curve[c][k];
            }
        } else {
            std::memset(impl_->residue_ch[c], 0, n2 * sizeof(float));
        }
    }

    // 5. IMDCT
    VorbisMdct& mdct = (blocksize == impl_->setup.blocksize_1) ? impl_->mdct_long : impl_->mdct_short;
    for (uint8_t c = 0; c < channels; ++c) {
        mdct.inverse_imdct(impl_->residue_ch[c], impl_->imdct_out[c]);
    }

    // 6. Windowing
    vorbis_generate_slope_window(impl_->window, blocksize, prev_bs, next_bs);
    for (uint8_t c = 0; c < channels; ++c) {
        for (size_t i = 0; i < blocksize; ++i) {
            impl_->imdct_out[c][i] *= impl_->window[i];
        }
    }

    // 7. Overlap-add
    size_t out_samples = 0;
    if (impl_->prev_blocksize > 0) {
        size_t overlap_center_prev = prev_bs / 4;
        size_t overlap_center_curr = blocksize / 4;
        size_t window_overlap = (prev_bs + blocksize) / 4;

        if (window_overlap * channels > max_out_samples) {
            return -1; // Not enough output space
        }

        for (size_t i = 0; i < window_overlap; ++i) {
            for (uint8_t c = 0; c < channels; ++c) {
                float sample = impl_->overlap_buffer[c][i] + impl_->imdct_out[c][overlap_center_curr - overlap_center_prev + i];
                out_pcm[out_samples++] = sample;
            }
        }
    }

    // Save remaining right half to overlap buffer for next block
    size_t next_overlap = blocksize / 2;
    for (uint8_t c = 0; c < channels; ++c) {
        for (size_t i = 0; i < next_overlap; ++i) {
            impl_->overlap_buffer[c][i] = impl_->imdct_out[c][blocksize / 2 + i];
        }
    }

    impl_->prev_blocksize = blocksize;
    return static_cast<int>(out_samples);
}

int VorbisDecoder::decode_ogg_page(const uint8_t* page_data, size_t page_len, 
                                   float* out_pcm, size_t max_out_samples) {
    if (!impl_ || !page_data || page_len == 0 || !out_pcm) return -1;

    size_t offset = 0;
    int total_pcm_out = 0;

    while (offset < page_len) {
        size_t consumed = 0;
        if (!impl_->demuxer.push_bytes(page_data + offset, page_len - offset, consumed)) {
            return -1;
        }
        if (consumed == 0 && !impl_->demuxer.has_packet()) {
            break;
        }
        offset += consumed;

        while (true) {
            int64_t granule_pos = 0;
            bool is_bos = false, is_eos = false;
            int pkt_len = impl_->demuxer.read_packet(impl_->packet_buffer, sizeof(impl_->packet_buffer), 
                                                     granule_pos, is_bos, is_eos);
            if (pkt_len <= 0) break;

            const uint8_t* pkt = impl_->packet_buffer;
            if (is_vorbis_header(pkt, pkt_len, VORBIS_PACKET_ID) ||
                is_vorbis_header(pkt, pkt_len, VORBIS_PACKET_COMMENT) ||
                is_vorbis_header(pkt, pkt_len, VORBIS_PACKET_SETUP)) {
                parse_header_packet(pkt, pkt_len);
            } else if (has_headers()) {
                int ret = decode_packet(pkt, pkt_len, out_pcm + total_pcm_out, max_out_samples - total_pcm_out);
                if (ret > 0) {
                    total_pcm_out += ret;
                }
                if (is_eos && total_pcm_out == 0 && impl_->prev_blocksize > 0) {
                    // Flush overlap buffer on EOS for single-packet streams
                    size_t fl_samples = impl_->prev_blocksize / 2;
                    for (size_t i = 0; i < fl_samples && (total_pcm_out + impl_->info.channels) <= max_out_samples; ++i) {
                        for (uint8_t c = 0; c < impl_->info.channels; ++c) {
                            out_pcm[total_pcm_out++] = impl_->overlap_buffer[c][i];
                        }
                    }
                }
            }
        }
    }

    return total_pcm_out;
}

int VorbisDecoder::decode_frame(const uint8_t* in_data, size_t in_bytes, 
                                float* out_pcm, size_t max_out_samples) {
    if (!in_data || in_bytes == 0 || !out_pcm) return -1;

    // Auto-detect Ogg framing (begins with "OggS" or already in Ogg stream mode)
    if (impl_->is_ogg_stream || (in_bytes >= 4 && in_data[0] == 'O' && in_data[1] == 'g' && in_data[2] == 'g' && in_data[3] == 'S')) {
        impl_->is_ogg_stream = true;
        return decode_ogg_page(in_data, in_bytes, out_pcm, max_out_samples);
    } else {
        return decode_packet(in_data, in_bytes, out_pcm, max_out_samples);
    }
}

} // namespace audio_codecs::vorbis
