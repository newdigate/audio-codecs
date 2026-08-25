#pragma once
#include "src/core/bit_reader.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::mp3 {

class BitReservoir {
public:
    static constexpr size_t BUFFER_SIZE = 4096;

    BitReservoir();

    void reset();

    // Append raw main_data bytes from the current frame to reservoir
    void append_main_data(const uint8_t* data, size_t bytes);

    // Prepare a BitReader for reading main_data using the backpointer offset
    // main_data_begin: negative byte offset relative to start of current frame's main data
    // current_frame_bytes: number of main_data bytes in the current frame
    // num_bytes: total main_data bytes needed for this frame
    bool prepare_reader(size_t main_data_begin, size_t current_frame_bytes, size_t num_bytes, 
                        core::BitReader& out_reader, uint8_t* scratch_buf);

    size_t get_size() const { return size_; }

private:
    uint8_t buffer_[BUFFER_SIZE];
    size_t size_{0};
};

} // namespace audio_codecs::mp3
