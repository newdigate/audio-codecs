#pragma once

#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/mp4/mp4_types.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace audio_codecs::mp4 {

class Mp4Muxer {
public:
    Mp4Muxer();
    ~Mp4Muxer();

    Mp4Muxer(const Mp4Muxer&) = delete;
    Mp4Muxer& operator=(const Mp4Muxer&) = delete;
    Mp4Muxer(Mp4Muxer&&) noexcept;
    Mp4Muxer& operator=(Mp4Muxer&&) noexcept;

    // Initialize muxer with audio stream configuration
    bool init(const AudioConfig& config);

    // Append a raw AAC audio frame payload into the media data stream
    bool add_sample(const uint8_t* sample_data, size_t sample_size);

    // Finalize the ISOBMFF container and produce standard .m4a binary
    std::vector<uint8_t> finalize();

    // Reset muxer state
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audio_codecs::mp4
