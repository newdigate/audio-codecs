#pragma once

#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/mp4/mp4_types.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace audio_codecs::mp4 {

class Mp4Demuxer {
public:
    Mp4Demuxer();
    ~Mp4Demuxer();

    Mp4Demuxer(const Mp4Demuxer&) = delete;
    Mp4Demuxer& operator=(const Mp4Demuxer&) = delete;
    Mp4Demuxer(Mp4Demuxer&&) noexcept;
    Mp4Demuxer& operator=(Mp4Demuxer&&) noexcept;

    // Open and parse an ISOBMFF / MP4 container in memory
    bool open(const uint8_t* data, size_t size);

    // Retrieve audio configuration (sample rate, channels, etc.)
    bool get_audio_config(AudioConfig& config) const;

    // Retrieve AudioSpecificConfig
    bool get_asc(AudioSpecificConfig& asc) const;

    // Total number of audio samples (frames) in the container
    size_t get_sample_count() const;

    // Random access to a specific sample by 0-based index
    bool read_sample(size_t sample_index, const uint8_t*& sample_ptr, size_t& sample_size) const;

    // Sequential streaming access to the next sample
    bool read_next_sample(const uint8_t*& sample_ptr, size_t& sample_size);

    // Reset sequential reading cursor back to the first sample (index 0)
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audio_codecs::mp4
