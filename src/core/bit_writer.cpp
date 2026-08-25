#include "src/core/bit_writer.h"
#include <cstring>
#include <algorithm>

namespace audio_codecs::core {

void BitWriter::init(uint8_t* buffer, size_t max_bytes) {
    buffer_ = buffer;
    max_bits_ = max_bytes * 8;
    bit_pos_ = 0;
    if (buffer_ && max_bytes > 0) {
        std::memset(buffer_, 0, max_bytes);
    }
}

void BitWriter::write_bits(uint32_t value, int n) {
    if (n <= 0 || !buffer_ || bit_pos_ >= max_bits_) return;
    
    int bits_to_write = std::min(n, static_cast<int>(max_bits_ - bit_pos_));
    // Mask value to n bits
    value &= (n == 32) ? 0xFFFFFFFFU : ((1U << n) - 1U);
    
    for (int i = bits_to_write - 1; i >= 0; --i) {
        size_t byte_idx = bit_pos_ >> 3;
        int bit_idx = 7 - static_cast<int>(bit_pos_ & 7);
        uint8_t bit = static_cast<uint8_t>((value >> (n - 1 - (bits_to_write - 1 - i))) & 1U);
        
        if (bit) {
            buffer_[byte_idx] |= (1U << bit_idx);
        } else {
            buffer_[byte_idx] &= ~(1U << bit_idx);
        }
        bit_pos_++;
    }
}

void BitWriter::flush_to_byte() {
    size_t rem = bit_pos_ & 7;
    if (rem != 0) {
        bit_pos_ += (8 - rem);
    }
}

} // namespace audio_codecs::core
