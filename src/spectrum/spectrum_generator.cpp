#include "audio_codecs/spectrum/spectrum_generator.h"
#include <cstring>
#include <algorithm>

namespace audio_codecs::spectrum {

SpectrumGenerator::SpectrumGenerator() = default;

bool SpectrumGenerator::init(preview::SeekableReader& pcm_source,
                             preview::SeekableWriter& spectrum_dest,
                             FftBackend& fft_backend,
                             uint32_t sample_rate,
                             uint8_t source_channels,
                             bool downmix_to_mono,
                             uint8_t num_bands) {
    status_ = GeneratorStatus::Ready;
    source_ = nullptr;
    dest_ = nullptr;
    backend_ = nullptr;
    total_pcm_frames_read_ = 0;
    lod0_frame_count_ = 0;
    lod1_frame_count_ = 0;
    lod0_offset_ = 128;
    lod1_offset_ = 0;
    lod1_current_group_ = 0;
    std::memset(pcm_window_, 0, sizeof(pcm_window_));
    std::memset(windowed_pcm_, 0, sizeof(windowed_pcm_));
    std::memset(magnitudes_, 0, sizeof(magnitudes_));
    std::memset(frame_bands_, 0, sizeof(frame_bands_));

    if (sample_rate == 0 || source_channels == 0 || source_channels > 2 || num_bands == 0) {
        status_ = (source_channels == 0 || source_channels > 2) ? GeneratorStatus::ErrorSource : GeneratorStatus::ErrorBackend;
        return false;
    }

    source_ = &pcm_source;
    dest_ = &spectrum_dest;
    backend_ = &fft_backend;
    sample_rate_ = sample_rate;
    source_channels_ = source_channels;
    downmix_to_mono_ = downmix_to_mono;
    num_bands_ = (num_bands > 64) ? 64 : num_bands;

    if (!filterbank_.init(sample_rate_, num_bands_)) {
        status_ = GeneratorStatus::ErrorBackend;
        return false;
    }

    // Reserve 128 bytes for header
    AsvHeader blank_hdr{};
    if (!dest_->seek(0) || dest_->write(reinterpret_cast<const uint8_t*>(&blank_hdr), sizeof(blank_hdr)) != sizeof(blank_hdr)) {
        status_ = GeneratorStatus::ErrorDest;
        return false;
    }

    status_ = GeneratorStatus::ProcessingLOD0;
    return true;
}

GeneratorStatus SpectrumGenerator::step(size_t frame_budget) {
    if (status_ == GeneratorStatus::ProcessingLOD0) {
        if (!source_ || !dest_ || !backend_) {
            status_ = GeneratorStatus::ErrorSource;
            return status_;
        }

        size_t frames_processed = 0;
        while (frames_processed < frame_budget) {
            // Shift overlap window by 512 samples
            std::memmove(pcm_window_, pcm_window_ + 512, 512 * sizeof(int16_t));

            // Read next 512 samples
            int16_t raw_in[512 * 2];
            size_t bytes_to_read = 512 * source_channels_ * sizeof(int16_t);
            size_t bytes_read = source_->read(reinterpret_cast<uint8_t*>(raw_in), bytes_to_read);
            size_t frames_read = bytes_read / (source_channels_ * sizeof(int16_t));

            if (frames_read == 0) {
                // LOD 0 complete
                lod1_offset_ = dest_->position();
                status_ = GeneratorStatus::ProcessingLOD1;
                break;
            }

            // Downmix to mono and place in upper half
            for (size_t i = 0; i < frames_read; ++i) {
                if (source_channels_ == 1) {
                    pcm_window_[512 + i] = raw_in[i];
                } else if (downmix_to_mono_) {
                    int32_t sum = static_cast<int32_t>(raw_in[i * 2]) + static_cast<int32_t>(raw_in[i * 2 + 1]);
                    pcm_window_[512 + i] = static_cast<int16_t>(sum / 2);
                } else {
                    pcm_window_[512 + i] = raw_in[i * 2];
                }
            }
            for (size_t i = frames_read; i < 512; ++i) {
                pcm_window_[512 + i] = 0;
            }

            total_pcm_frames_read_ += frames_read;

            // Apply window and FFT
            filterbank_.apply_hann_window(pcm_window_, windowed_pcm_);
            backend_->forward_1024(windowed_pcm_, magnitudes_);
            filterbank_.compute_bands(magnitudes_, frame_bands_);

            if (dest_->write(frame_bands_, num_bands_) != num_bands_) {
                status_ = GeneratorStatus::ErrorDest;
                return status_;
            }

            lod0_frame_count_++;
            frames_processed++;
        }
        return status_;
    }

    if (status_ == GeneratorStatus::ProcessingLOD1) {
        auto* reader = dynamic_cast<preview::SeekableReader*>(dest_);
        if (!reader) {
            status_ = GeneratorStatus::ErrorDest;
            return status_;
        }

        uint32_t total_groups = (lod0_frame_count_ + 15) / 16;
        size_t groups_processed = 0;

        while (lod1_current_group_ < total_groups && groups_processed < frame_budget) {
            uint32_t start_idx = lod1_current_group_ * 16;
            uint32_t count = std::min(16u, lod0_frame_count_ - start_idx);

            uint8_t group_bands[64] = {0};

            for (uint32_t i = 0; i < count; ++i) {
                uint64_t offset = lod0_offset_ + static_cast<uint64_t>(start_idx + i) * num_bands_;
                if (!reader->seek(offset)) {
                    status_ = GeneratorStatus::ErrorDest;
                    return status_;
                }

                uint8_t src_bands[64];
                if (reader->read(src_bands, num_bands_) != num_bands_) {
                    status_ = GeneratorStatus::ErrorDest;
                    return status_;
                }

                for (size_t b = 0; b < num_bands_; ++b) {
                    group_bands[b] = std::max(group_bands[b], src_bands[b]);
                }
            }

            // Write LOD 1 frame to end of dest
            uint64_t write_pos = lod1_offset_ + static_cast<uint64_t>(lod1_current_group_) * num_bands_;
            if (!dest_->seek(write_pos) || dest_->write(group_bands, num_bands_) != num_bands_) {
                status_ = GeneratorStatus::ErrorDest;
                return status_;
            }

            lod1_current_group_++;
            lod1_frame_count_++;
            groups_processed++;
        }

        if (lod1_current_group_ >= total_groups) {
            status_ = GeneratorStatus::Finalizing;
        }
        return status_;
    }

    if (status_ == GeneratorStatus::Finalizing) {
        if (!dest_) {
            status_ = GeneratorStatus::ErrorDest;
            return status_;
        }

        AsvHeader hdr{};
        hdr.magic = ASV_MAGIC;
        hdr.version = ASV_VERSION;
        hdr.flags = ASV_FLAG_HAS_LODS;
        hdr.sample_rate = sample_rate_;
        hdr.channels = (downmix_to_mono_ || source_channels_ == 1) ? 1 : 2;
        if (hdr.channels == 2) {
            hdr.flags |= ASV_FLAG_STEREO;
        }
        hdr.num_bands = num_bands_;
        hdr.fft_size = DEFAULT_FFT_SIZE;
        hdr.hop_size = DEFAULT_HOP_SIZE;
        hdr.min_freq_hz = 20;
        hdr.max_freq_hz = 20000;
        hdr.lod_count = 2;
        hdr.total_pcm_frames = total_pcm_frames_read_;
        hdr.duration_ms = (sample_rate_ > 0) ? static_cast<uint32_t>((total_pcm_frames_read_ * 1000ULL) / sample_rate_) : 0;

        hdr.lods[0].downsample_ratio = 1;
        hdr.lods[0].frame_count = lod0_frame_count_;
        hdr.lods[0].file_offset = lod0_offset_;

        hdr.lods[1].downsample_ratio = 16;
        hdr.lods[1].frame_count = lod1_frame_count_;
        hdr.lods[1].file_offset = lod1_offset_;

        if (!dest_->seek(0) || dest_->write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
            status_ = GeneratorStatus::ErrorDest;
            return status_;
        }

        dest_->flush();
        status_ = GeneratorStatus::Complete;
    }

    return status_;
}

bool SpectrumGenerator::generate_all() {
    if (status_ == GeneratorStatus::Ready) {
        return false;
    }
    while (status_ != GeneratorStatus::Complete &&
           status_ != GeneratorStatus::ErrorSource &&
           status_ != GeneratorStatus::ErrorDest &&
           status_ != GeneratorStatus::ErrorBackend) {
        step(64);
    }
    return status_ == GeneratorStatus::Complete;
}

float SpectrumGenerator::progress() const {
    if (status_ == GeneratorStatus::Ready) return 0.0f;
    if (status_ == GeneratorStatus::Complete) return 1.0f;
    if (status_ == GeneratorStatus::ProcessingLOD0) {
        if (source_ && source_->size() > 0) {
            float p = 0.8f * (static_cast<float>(source_->position()) / static_cast<float>(source_->size()));
            return std::clamp(p, 0.0f, 0.8f);
        }
        return 0.1f;
    }
    if (status_ == GeneratorStatus::ProcessingLOD1) {
        uint32_t total_groups = (lod0_frame_count_ + 15) / 16;
        if (total_groups == 0) return 0.8f;
        float p = 0.8f + 0.2f * (static_cast<float>(lod1_current_group_) / static_cast<float>(total_groups));
        return std::clamp(p, 0.8f, 1.0f);
    }
    if (status_ == GeneratorStatus::Finalizing) return 0.99f;
    return 0.0f;
}

} // namespace audio_codecs::spectrum
