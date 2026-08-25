#include "audio_codecs/vorbis/vorbis_encoder.h"
#include "src/vorbis/encoder/setup_builder.h"
#include "src/vorbis/vorbis_mdct.h"
#include "src/vorbis/vorbis_common.h"
#include "src/vorbis/vorbis_floor.h"
#include "src/vorbis/vorbis_residue.h"
#include "src/vorbis/vorbis_mapping.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace audio_codecs::vorbis {

struct VorbisEncoder::Impl {
    ogg::OggMuxer muxer{0x01020304};
    VorbisInfo info{};
    VorbisSetup setup{};

    VorbisMdct mdct_short;
    VorbisMdct mdct_long;

    bool headers_emitted{false};
    bool initialized{false};

    size_t prev_blocksize{512};
    size_t history_samples{0};
    int64_t current_granule_pos{0};

    // Sliding PCM buffer per channel
    alignas(16) float pcm_history[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE * 2]{{0}};

    // Scratch buffers
    alignas(16) float mdct_in[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE]{{0}};
    alignas(16) float spectrum[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE / 2]{{0}};
    alignas(16) float residue[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE / 2]{{0}};
    alignas(16) float floor_curve[VORBIS_MAX_CHANNELS][VORBIS_MAX_BLOCK_SIZE / 2]{{0}};
    int32_t floor_y[VORBIS_MAX_CHANNELS][VORBIS_MAX_FLOOR1_POSTS]{{0}};
    bool ch_nonzero[VORBIS_MAX_CHANNELS]{true, true};

    alignas(16) float window[VORBIS_MAX_BLOCK_SIZE]{0};
    alignas(16) uint8_t packet_buffer[65536]{0};
    alignas(16) uint8_t header_buffer[4096]{0};

    void reset() {
        muxer.reset();
        headers_emitted = false;
        history_samples = 0;
        current_granule_pos = 0;
        prev_blocksize = setup.blocksize_0;
        std::memset(pcm_history, 0, sizeof(pcm_history));
    }
};

VorbisEncoder::VorbisEncoder() {
    static_assert(sizeof(Impl) <= sizeof(state_buffer_), "state_buffer_ too small for VorbisEncoder::Impl");
    impl_ = new (state_buffer_) Impl();
}

VorbisEncoder::~VorbisEncoder() {
    if (impl_) {
        impl_->~Impl();
        impl_ = nullptr;
    }
}

bool VorbisEncoder::init(const AudioConfig& config) {
    if (!impl_) return false;

    impl_->info.channels = static_cast<uint8_t>(config.channels > 0 ? config.channels : 2);
    impl_->info.sample_rate = config.sample_rate > 0 ? config.sample_rate : 44100;
    impl_->info.bitrate_nominal = config.bitrate_kbps > 0 ? static_cast<int32_t>(config.bitrate_kbps * 1000) : 128000;
    impl_->info.blocksize_0 = 512;
    impl_->info.blocksize_1 = 2048;

    // Build standard setup
    build_vorbis_setup_header(impl_->header_buffer, sizeof(impl_->header_buffer), impl_->info, impl_->setup);

    impl_->mdct_short.init(impl_->info.blocksize_0);
    impl_->mdct_long.init(impl_->info.blocksize_1);

    impl_->reset();
    impl_->initialized = true;
    return true;
}

void VorbisEncoder::reset() {
    if (impl_) {
        impl_->reset();
    }
}

const VorbisInfo& VorbisEncoder::get_info() const {
    return impl_->info;
}

size_t VorbisEncoder::write_headers(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !out_data || max_out_bytes < 1024) return 0;

    // 1. Identification Header
    uint8_t id_buf[64];
    size_t id_len = build_vorbis_id_header(id_buf, sizeof(id_buf), impl_->info);
    impl_->muxer.write_packet(id_buf, id_len, true, false, 0); // BOS = true
    int p1 = impl_->muxer.flush_page(out_data, max_out_bytes, true);
    if (p1 <= 0) return 0;

    // 2. Comment Header
    uint8_t comment_buf[128];
    size_t comment_len = build_vorbis_comment_header(comment_buf, sizeof(comment_buf));
    impl_->muxer.write_packet(comment_buf, comment_len, false, false, 0);

    // 3. Setup Header
    uint8_t setup_buf[4096];
    size_t setup_len = build_vorbis_setup_header(setup_buf, sizeof(setup_buf), impl_->info, impl_->setup);
    impl_->muxer.write_packet(setup_buf, setup_len, false, false, 0);

    int p2 = impl_->muxer.flush_page(out_data + p1, max_out_bytes - p1, true);
    impl_->headers_emitted = true;

    return p1 + (p2 > 0 ? p2 : 0);
}

int VorbisEncoder::encode_packet(const float* const* in_channel_pcm, size_t blocksize,
                                 uint8_t* out_packet, size_t max_out_bytes) {
    if (!impl_ || !in_channel_pcm || !out_packet || max_out_bytes == 0) return -1;

    size_t n = blocksize;
    size_t n2 = n / 2;
    uint8_t channels = impl_->info.channels;
    uint8_t mode_idx = (blocksize == impl_->setup.blocksize_1) ? 1 : 0;
    const VorbisMode& mode = impl_->setup.modes[mode_idx];
    const VorbisMappingConfig& mapping = impl_->setup.mappings[mode.mapping];

    // 1. Window and MDCT for each channel
    size_t prev_bs = impl_->prev_blocksize;
    size_t next_bs = blocksize;
    vorbis_generate_slope_window(impl_->window, n, prev_bs, next_bs);

    VorbisMdct& mdct = mode.blockflag ? impl_->mdct_long : impl_->mdct_short;

    for (uint8_t c = 0; c < channels; ++c) {
        for (size_t i = 0; i < n; ++i) {
            impl_->mdct_in[c][i] = in_channel_pcm[c][i] * impl_->window[i];
        }
        mdct.forward_mdct(impl_->mdct_in[c], impl_->spectrum[c]);
    }

    // 2. Floor fitting and spectral envelope division
    for (uint8_t c = 0; c < channels; ++c) {
        uint8_t submap = mapping.ch_mux[c];
        uint8_t floor_idx = mapping.submap_floor[submap];
        const VorbisFloor1Config& floor_cfg = impl_->setup.floors[floor_idx];

        vorbis_floor1_fit(impl_->spectrum[c], n2, floor_cfg, impl_->floor_y[c]);
        vorbis_floor1_render(floor_cfg, impl_->floor_y[c], impl_->floor_curve[c], n2);

        for (size_t k = 0; k < n2; ++k) {
            float env = std::max(1e-6f, impl_->floor_curve[c][k]);
            impl_->residue[c][k] = impl_->spectrum[c][k] / env;
        }
        impl_->ch_nonzero[c] = true;
    }

    // 3. Polar channel coupling if stereo
    float* res_ptrs[VORBIS_MAX_CHANNELS];
    for (uint8_t c = 0; c < channels; ++c) {
        res_ptrs[c] = impl_->residue[c];
    }
    vorbis_mapping_couple(mapping, res_ptrs, n2);

    // 4. Assemble Vorbis audio packet bitstream
    VorbisBitWriter writer(out_packet, max_out_bytes);
    writer.write_bits(0, 1); // Audio packet = 0

    uint32_t mode_bits = vorbis_ilog(impl_->setup.mode_count - 1);
    if (mode_bits > 0) {
        writer.write_bits(mode_idx, mode_bits);
    }

    if (mode.blockflag) {
        writer.write_bits(0, 1); // prev_window_flag
        writer.write_bits(0, 1); // next_window_flag
    }

    // Encode Floors
    for (uint8_t c = 0; c < channels; ++c) {
        uint8_t submap = mapping.ch_mux[c];
        uint8_t floor_idx = mapping.submap_floor[submap];
        vorbis_floor1_encode(writer, impl_->setup.floors[floor_idx], 
                             impl_->setup.codebooks, impl_->setup.codebook_count,
                             impl_->floor_y[c]);
    }

    // Encode Residues
    for (size_t s = 0; s < mapping.submaps; ++s) {
        uint8_t res_idx = mapping.submap_residue[s];
        vorbis_residue_encode(writer, impl_->setup.residues[res_idx],
                              impl_->setup.codebooks, impl_->setup.codebook_count,
                              res_ptrs, impl_->ch_nonzero, channels, n2);
    }

    writer.flush();
    impl_->prev_blocksize = blocksize;
    return static_cast<int>(writer.bytes_written());
}

int VorbisEncoder::encode_frame(const float* in_pcm, size_t in_samples,
                                uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !in_pcm || in_samples == 0 || !out_data) return -1;

    size_t total_bytes_written = 0;

    // Emit headers if not yet done
    if (!impl_->headers_emitted) {
        size_t h_bytes = write_headers(out_data, max_out_bytes);
        if (h_bytes == 0) return -1;
        total_bytes_written += h_bytes;
    }

    uint8_t channels = impl_->info.channels;
    size_t blocksize = impl_->setup.blocksize_0; // 512 short block default
    size_t hop_size = blocksize / 2;

    size_t in_frames = in_samples / channels;
    for (size_t i = 0; i < in_frames; ++i) {
        for (uint8_t c = 0; c < channels; ++c) {
            impl_->pcm_history[c][impl_->history_samples] = in_pcm[i * channels + c];
        }
        impl_->history_samples++;

        if (impl_->history_samples >= blocksize) {
            // Encode packet
            const float* ch_ptrs[VORBIS_MAX_CHANNELS];
            for (uint8_t c = 0; c < channels; ++c) {
                ch_ptrs[c] = impl_->pcm_history[c];
            }

            int pkt_len = encode_packet(ch_ptrs, blocksize, impl_->packet_buffer, sizeof(impl_->packet_buffer));
            if (pkt_len > 0) {
                impl_->current_granule_pos += hop_size;
                impl_->muxer.write_packet(impl_->packet_buffer, pkt_len, false, false, impl_->current_granule_pos);

                // Flush ready Ogg pages
                int page_bytes = impl_->muxer.flush_page(
                    out_data + total_bytes_written, 
                    max_out_bytes - total_bytes_written, 
                    false
                );
                if (page_bytes > 0) {
                    total_bytes_written += page_bytes;
                }
            }

            // Shift sliding history buffer by hop_size
            for (uint8_t c = 0; c < channels; ++c) {
                std::memmove(impl_->pcm_history[c], impl_->pcm_history[c] + hop_size, 
                             (impl_->history_samples - hop_size) * sizeof(float));
            }
            impl_->history_samples -= hop_size;
        }
    }

    return static_cast<int>(total_bytes_written);
}

int VorbisEncoder::flush(uint8_t* out_data, size_t max_out_bytes) {
    if (!impl_ || !out_data) return -1;

    size_t total = 0;
    // If pending samples in history, flush remaining block
    if (impl_->history_samples > 0) {
        size_t blocksize = impl_->setup.blocksize_0;
        for (uint8_t c = 0; c < impl_->info.channels; ++c) {
            for (size_t i = impl_->history_samples; i < blocksize; ++i) {
                impl_->pcm_history[c][i] = 0.0f;
            }
        }
        const float* ch_ptrs[VORBIS_MAX_CHANNELS];
        for (uint8_t c = 0; c < impl_->info.channels; ++c) {
            ch_ptrs[c] = impl_->pcm_history[c];
        }
        int pkt_len = encode_packet(ch_ptrs, blocksize, impl_->packet_buffer, sizeof(impl_->packet_buffer));
        if (pkt_len > 0) {
            impl_->current_granule_pos += blocksize / 2;
            impl_->muxer.write_packet(impl_->packet_buffer, pkt_len, false, true, impl_->current_granule_pos);
        }
        impl_->history_samples = 0;
    }

    int page_bytes = impl_->muxer.flush_page(out_data, max_out_bytes, true);
    if (page_bytes > 0) {
        total += page_bytes;
    }
    return static_cast<int>(total);
}

} // namespace audio_codecs::vorbis
