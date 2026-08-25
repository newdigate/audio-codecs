#include "src/mp3/decoder/bit_reservoir.h"
#include <cstring>
#include <algorithm>

namespace audio_codecs::mp3 {

BitReservoir::BitReservoir() {
    reset();
}

void BitReservoir::reset() {
    size_ = 0;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void BitReservoir::append_main_data(const uint8_t* data, size_t bytes) {
    if (!data || bytes == 0) return;

    if (size_ + bytes > BUFFER_SIZE) {
        // Shift data to leave space for maximum backpointer (e.g. 1024 bytes)
        size_t keep_bytes = std::min(size_, static_cast<size_t>(1024));
        std::memmove(buffer_, buffer_ + (size_ - keep_bytes), keep_bytes);
        size_ = keep_bytes;
    }

    size_t copy_bytes = std::min(bytes, BUFFER_SIZE - size_);
    std::memcpy(buffer_ + size_, data, copy_bytes);
    size_ += copy_bytes;
}

bool BitReservoir::prepare_reader(size_t main_data_begin, size_t current_frame_bytes, size_t num_bytes, 
                                  core::BitReader& out_reader, uint8_t* scratch_buf) {
    if (!scratch_buf || num_bytes == 0) {
        return false;
    }

    size_t current_frame_start = (size_ >= current_frame_bytes) ? (size_ - current_frame_bytes) : 0;
    
    if (current_frame_start >= main_data_begin) {
        size_t start_pos = current_frame_start - main_data_begin;
        size_t available_bytes = size_ - start_pos;
        size_t copy_bytes = std::min(num_bytes, available_bytes);

        std::memcpy(scratch_buf, buffer_ + start_pos, copy_bytes);
        if (copy_bytes < num_bytes) {
            std::memset(scratch_buf + copy_bytes, 0, num_bytes - copy_bytes);
        }
    } else {
        // Stream startup / reservoir underflow: pad missing prefix with zeros
        size_t missing_prefix = main_data_begin - current_frame_start;
        size_t prefix_to_pad = std::min(missing_prefix, num_bytes);
        std::memset(scratch_buf, 0, prefix_to_pad);

        if (num_bytes > prefix_to_pad) {
            size_t remaining_to_copy = num_bytes - prefix_to_pad;
            size_t copy_bytes = std::min(remaining_to_copy, size_);
            std::memcpy(scratch_buf + prefix_to_pad, buffer_, copy_bytes);
            if (prefix_to_pad + copy_bytes < num_bytes) {
                std::memset(scratch_buf + prefix_to_pad + copy_bytes, 0, num_bytes - (prefix_to_pad + copy_bytes));
            }
        }
    }

    out_reader.init(scratch_buf, num_bytes);
    return true;
}

} // namespace audio_codecs::mp3
