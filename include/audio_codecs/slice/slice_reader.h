#pragma once

#include <audio_codecs/slice/slice_types.h>
#include <cstdint>
#include <cstddef>

namespace audio_codecs::slice {

class SliceReader {
public:
    SliceReader();
    bool init(const uint8_t* asl_data, size_t asl_size);

    const AslHeader& header() const;
    uint16_t total_slices() const;

    // O(1) direct slice access
    const AslSlice* get_slice(uint16_t index) const;

    // O(log N) binary search queries
    const AslSlice* find_slice_at_sample(uint32_t sample_offset) const;
    const AslSlice* find_slice_at_ms(uint32_t ms) const;
    const AslSlice* find_slice_at_tick(uint32_t tick) const;

private:
    const AslHeader* header_ = nullptr;
    const AslSlice* slices_ = nullptr;
    uint16_t total_slices_ = 0;
    size_t data_size_ = 0;
};

} // namespace audio_codecs::slice
