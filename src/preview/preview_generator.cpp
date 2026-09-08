#include "audio_codecs/preview/preview_generator.h"
#include <cstring>

namespace audio_codecs::preview {

PreviewGenerator::PreviewGenerator() = default;

bool PreviewGenerator::init(SeekableReader& audio_source,
                            SeekableWriter& preview_dest,
                            AudioDecoder* decoder,
                            uint32_t sample_rate,
                            uint8_t channels,
                            bool stereo) {
    if (channels == 0 || channels > 2) return false;

    source_ = &audio_source;
    dest_ = &preview_dest;
    decoder_ = decoder;
    sample_rate_ = sample_rate;
    channels_ = channels;
    stereo_ = stereo && (channels > 1);

    std::memset(&header_, 0, sizeof(header_));
    header_.magic = APV_MAGIC;
    header_.version = APV_VERSION;
    header_.flags = (stereo_ ? APV_FLAG_STEREO : 0) | APV_FLAG_HAS_LODS;
    header_.sample_rate = sample_rate_;
    header_.channels = stereo_ ? 2 : 1;
    header_.bytes_per_chunk = stereo_ ? 4 : 2;
    header_.samples_per_base_chunk = BASE_CHUNK_FRAMES;
    header_.source_file_size = static_cast<uint32_t>(source_->size());

    total_frames_processed_ = 0;
    lod0_chunk_count_ = 0;
    lod1_chunk_count_ = 0;
    lod2_chunk_count_ = 0;
    state_ = State::Init;
    return true;
}

GeneratorStatus PreviewGenerator::step(size_t chunk_budget) {
    switch (state_) {
        case State::Init: {
            if (!dest_->seek(0)) return GeneratorStatus::ErrorDest;
            uint8_t zero_hdr[128]{0};
            if (dest_->write(zero_hdr, sizeof(zero_hdr)) != sizeof(zero_hdr)) {
                return GeneratorStatus::ErrorDest;
            }
            header_.lods[0].file_offset = 128;
            header_.lods[0].downsample_ratio = 1;
            state_ = State::DecodeLOD0;
            return GeneratorStatus::Working;
        }

        case State::DecodeLOD0: {
            size_t chunks_done = 0;
            while (chunks_done < chunk_budget) {
                // Read 128 frames (512 bytes if 16-bit stereo)
                size_t frames_to_read = BASE_CHUNK_FRAMES;
                size_t bytes_needed = frames_to_read * channels_ * sizeof(int16_t);
                size_t bytes_read = source_->read(reinterpret_cast<uint8_t*>(frame_buf_), bytes_needed);
                if (bytes_read == 0) {
                    header_.lods[0].chunk_count = lod0_chunk_count_;
                    state_ = State::GenerateLOD1;
                    return GeneratorStatus::Working;
                }

                size_t frames_read = bytes_read / (channels_ * sizeof(int16_t));
                if (frames_read == 0) {
                    header_.lods[0].chunk_count = lod0_chunk_count_;
                    state_ = State::GenerateLOD1;
                    return GeneratorStatus::Working;
                }

                // Compute peaks
                if (stereo_) {
                    int8_t l_min = 0, l_max = 0, r_min = 0, r_max = 0;
                    for (size_t f = 0; f < frames_read; ++f) {
                        int8_t l = scale_pcm(frame_buf_[f * channels_]);
                        int8_t r = scale_pcm(frame_buf_[f * channels_ + 1]);
                        if (l < l_min) l_min = l;
                        if (l > l_max) l_max = l;
                        if (r < r_min) r_min = r;
                        if (r > r_max) r_max = r;
                    }
                    WaveformPointStereo pt{l_min, l_max, r_min, r_max};
                    if (dest_->write(reinterpret_cast<const uint8_t*>(&pt), sizeof(pt)) != sizeof(pt)) {
                        return GeneratorStatus::ErrorDest;
                    }
                } else {
                    int8_t m_min = 0, m_max = 0;
                    for (size_t f = 0; f < frames_read; ++f) {
                        int16_t samp = frame_buf_[f * channels_];
                        if (channels_ > 1) {
                            samp = static_cast<int16_t>((samp + frame_buf_[f * channels_ + 1]) / 2);
                        }
                        int8_t m = scale_pcm(samp);
                        if (m < m_min) m_min = m;
                        if (m > m_max) m_max = m;
                    }
                    WaveformPointMono pt{m_min, m_max};
                    if (dest_->write(reinterpret_cast<const uint8_t*>(&pt), sizeof(pt)) != sizeof(pt)) {
                        return GeneratorStatus::ErrorDest;
                    }
                }

                total_frames_processed_ += frames_read;
                lod0_chunk_count_++;
                chunks_done++;
            }
            return GeneratorStatus::Working;
        }

        case State::GenerateLOD1: {
            uint64_t lod1_offset = header_.lods[0].file_offset + (static_cast<uint64_t>(lod0_chunk_count_) * header_.bytes_per_chunk);
            header_.lods[1].file_offset = lod1_offset;
            header_.lods[1].downsample_ratio = 16;

            if (lod0_chunk_count_ == 0) {
                header_.lods[1].chunk_count = 0;
                state_ = State::GenerateLOD2;
                return GeneratorStatus::Working;
            }

            SeekableReader* reader = dynamic_cast<SeekableReader*>(dest_);
            if (!reader) return GeneratorStatus::ErrorDest;

            uint32_t full_groups = lod0_chunk_count_ / 16;
            uint32_t remainder = lod0_chunk_count_ % 16;
            uint32_t total_lod1 = full_groups + (remainder ? 1 : 0);

            for (uint32_t g = 0; g < total_lod1; ++g) {
                uint32_t chunks_in_group = (g < full_groups) ? 16 : remainder;
                uint64_t read_offset = header_.lods[0].file_offset + (static_cast<uint64_t>(g) * 16 * header_.bytes_per_chunk);
                if (!dest_->seek(read_offset)) return GeneratorStatus::ErrorDest;

                if (stereo_) {
                    WaveformPointStereo group_chunks[16];
                    size_t bytes_to_read = chunks_in_group * sizeof(WaveformPointStereo);
                    if (reader->read(reinterpret_cast<uint8_t*>(group_chunks), bytes_to_read) != bytes_to_read) {
                        return GeneratorStatus::ErrorDest;
                    }

                    int8_t l_min = 0, l_max = 0, r_min = 0, r_max = 0;
                    for (uint32_t i = 0; i < chunks_in_group; ++i) {
                        if (group_chunks[i].left_min < l_min) l_min = group_chunks[i].left_min;
                        if (group_chunks[i].left_max > l_max) l_max = group_chunks[i].left_max;
                        if (group_chunks[i].right_min < r_min) r_min = group_chunks[i].right_min;
                        if (group_chunks[i].right_max > r_max) r_max = group_chunks[i].right_max;
                    }
                    WaveformPointStereo pt{l_min, l_max, r_min, r_max};
                    uint64_t write_offset = lod1_offset + (static_cast<uint64_t>(g) * sizeof(WaveformPointStereo));
                    if (!dest_->seek(write_offset)) return GeneratorStatus::ErrorDest;
                    if (dest_->write(reinterpret_cast<const uint8_t*>(&pt), sizeof(pt)) != sizeof(pt)) {
                        return GeneratorStatus::ErrorDest;
                    }
                } else {
                    WaveformPointMono group_chunks[16];
                    size_t bytes_to_read = chunks_in_group * sizeof(WaveformPointMono);
                    if (reader->read(reinterpret_cast<uint8_t*>(group_chunks), bytes_to_read) != bytes_to_read) {
                        return GeneratorStatus::ErrorDest;
                    }

                    int8_t m_min = 0, m_max = 0;
                    for (uint32_t i = 0; i < chunks_in_group; ++i) {
                        if (group_chunks[i].min < m_min) m_min = group_chunks[i].min;
                        if (group_chunks[i].max > m_max) m_max = group_chunks[i].max;
                    }
                    WaveformPointMono pt{m_min, m_max};
                    uint64_t write_offset = lod1_offset + (static_cast<uint64_t>(g) * sizeof(WaveformPointMono));
                    if (!dest_->seek(write_offset)) return GeneratorStatus::ErrorDest;
                    if (dest_->write(reinterpret_cast<const uint8_t*>(&pt), sizeof(pt)) != sizeof(pt)) {
                        return GeneratorStatus::ErrorDest;
                    }
                }
            }

            lod1_chunk_count_ = total_lod1;
            header_.lods[1].chunk_count = lod1_chunk_count_;
            state_ = State::GenerateLOD2;
            return GeneratorStatus::Working;
        }

        case State::GenerateLOD2: {
            uint64_t lod2_offset = header_.lods[1].file_offset + (static_cast<uint64_t>(lod1_chunk_count_) * header_.bytes_per_chunk);
            header_.lods[2].file_offset = lod2_offset;
            header_.lods[2].downsample_ratio = 256;

            if (lod1_chunk_count_ == 0) {
                header_.lods[2].chunk_count = 0;
                state_ = State::FinalizeHeader;
                return GeneratorStatus::Working;
            }

            SeekableReader* reader = dynamic_cast<SeekableReader*>(dest_);
            if (!reader) return GeneratorStatus::ErrorDest;

            uint32_t full_groups = lod1_chunk_count_ / 16;
            uint32_t remainder = lod1_chunk_count_ % 16;
            uint32_t total_lod2 = full_groups + (remainder ? 1 : 0);

            for (uint32_t g = 0; g < total_lod2; ++g) {
                uint32_t chunks_in_group = (g < full_groups) ? 16 : remainder;
                uint64_t read_offset = header_.lods[1].file_offset + (static_cast<uint64_t>(g) * 16 * header_.bytes_per_chunk);
                if (!dest_->seek(read_offset)) return GeneratorStatus::ErrorDest;

                if (stereo_) {
                    WaveformPointStereo group_chunks[16];
                    size_t bytes_to_read = chunks_in_group * sizeof(WaveformPointStereo);
                    if (reader->read(reinterpret_cast<uint8_t*>(group_chunks), bytes_to_read) != bytes_to_read) {
                        return GeneratorStatus::ErrorDest;
                    }

                    int8_t l_min = 0, l_max = 0, r_min = 0, r_max = 0;
                    for (uint32_t i = 0; i < chunks_in_group; ++i) {
                        if (group_chunks[i].left_min < l_min) l_min = group_chunks[i].left_min;
                        if (group_chunks[i].left_max > l_max) l_max = group_chunks[i].left_max;
                        if (group_chunks[i].right_min < r_min) r_min = group_chunks[i].right_min;
                        if (group_chunks[i].right_max > r_max) r_max = group_chunks[i].right_max;
                    }
                    WaveformPointStereo pt{l_min, l_max, r_min, r_max};
                    uint64_t write_offset = lod2_offset + (static_cast<uint64_t>(g) * sizeof(WaveformPointStereo));
                    if (!dest_->seek(write_offset)) return GeneratorStatus::ErrorDest;
                    if (dest_->write(reinterpret_cast<const uint8_t*>(&pt), sizeof(pt)) != sizeof(pt)) {
                        return GeneratorStatus::ErrorDest;
                    }
                } else {
                    WaveformPointMono group_chunks[16];
                    size_t bytes_to_read = chunks_in_group * sizeof(WaveformPointMono);
                    if (reader->read(reinterpret_cast<uint8_t*>(group_chunks), bytes_to_read) != bytes_to_read) {
                        return GeneratorStatus::ErrorDest;
                    }

                    int8_t m_min = 0, m_max = 0;
                    for (uint32_t i = 0; i < chunks_in_group; ++i) {
                        if (group_chunks[i].min < m_min) m_min = group_chunks[i].min;
                        if (group_chunks[i].max > m_max) m_max = group_chunks[i].max;
                    }
                    WaveformPointMono pt{m_min, m_max};
                    uint64_t write_offset = lod2_offset + (static_cast<uint64_t>(g) * sizeof(WaveformPointMono));
                    if (!dest_->seek(write_offset)) return GeneratorStatus::ErrorDest;
                    if (dest_->write(reinterpret_cast<const uint8_t*>(&pt), sizeof(pt)) != sizeof(pt)) {
                        return GeneratorStatus::ErrorDest;
                    }
                }
            }

            lod2_chunk_count_ = total_lod2;
            header_.lods[2].chunk_count = lod2_chunk_count_;
            state_ = State::FinalizeHeader;
            return GeneratorStatus::Working;
        }

        case State::FinalizeHeader: {
            header_.total_pcm_frames = total_frames_processed_;
            if (sample_rate_ > 0) {
                header_.duration_ms = static_cast<uint32_t>((total_frames_processed_ * 1000ULL) / sample_rate_);
            }
            header_.lod_count = 3;

            if (!dest_->seek(0)) return GeneratorStatus::ErrorDest;
            if (dest_->write(reinterpret_cast<const uint8_t*>(&header_), sizeof(header_)) != sizeof(header_)) {
                return GeneratorStatus::ErrorDest;
            }
            dest_->flush();
            state_ = State::Done;
            return GeneratorStatus::Complete;
        }

        case State::Done:
            return GeneratorStatus::Complete;

        case State::Error:
        default:
            return GeneratorStatus::ErrorDest;
    }
}

bool PreviewGenerator::generate_all() {
    while (true) {
        GeneratorStatus s = step(256);
        if (s == GeneratorStatus::Complete) return true;
        if (s != GeneratorStatus::Working) return false;
    }
}

float PreviewGenerator::progress() const {
    if (state_ == State::Done) return 100.0f;
    if (state_ == State::Init) return 0.0f;
    if (state_ == State::GenerateLOD1) return 90.0f;
    if (state_ == State::GenerateLOD2) return 95.0f;
    if (state_ == State::FinalizeHeader) return 99.0f;
    if (source_ && source_->size() > 0) {
        float p = (static_cast<float>(source_->position()) / static_cast<float>(source_->size())) * 85.0f;
        return std::clamp(p, 0.0f, 85.0f);
    }
    return 50.0f;
}

const ApvHeader& PreviewGenerator::header() const {
    return header_;
}

} // namespace audio_codecs::preview
