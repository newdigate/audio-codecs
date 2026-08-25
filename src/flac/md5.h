#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::flac {

class Md5Context {
public:
    Md5Context();

    void init();
    void update(const uint8_t* data, size_t len);
    void finish(uint8_t digest[16]);

private:
    uint32_t state_[4];
    uint64_t count_{0};
    uint8_t  buffer_[64];

    void transform(const uint8_t block[64]);
};

} // namespace audio_codecs::flac
