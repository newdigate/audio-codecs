#include "src/core/bit_reader.h"
#include <algorithm>

namespace audio_codecs::core {

void BitReader::init(const uint8_t* data, size_t num_bytes) {
    data_ = data;
    total_bits_ = num_bytes * 8;
    bit_pos_ = 0;
}

uint32_t BitReader::read_bits(int n) {
    if (n <= 0) return 0;
    uint32_t val = peek_bits(n);
    bit_pos_ += static_cast<size_t>(n);
    if (bit_pos_ > total_bits_) {
        bit_pos_ = total_bits_;
    }
    return val;
}

uint32_t BitReader::peek_bits(int n) {
    if (n <= 0 || !data_ || bit_pos_ >= total_bits_) return 0;
    
    int bits_to_read = std::min(n, static_cast<int>(total_bits_ - bit_pos_));
    uint32_t result = 0;
    size_t curr_bit = bit_pos_;
    
    for (int i = 0; i < bits_to_read; ++i) {
        size_t byte_idx = curr_bit >> 3;
        int bit_idx = 7 - static_cast<int>(curr_bit & 7);
        uint32_t bit = (data_[byte_idx] >> bit_idx) & 1U;
        result = (result << 1) | bit;
        curr_bit++;
    }
    
    if (bits_to_read < n) {
        result <<= (n - bits_to_read);
    }
    
    return result;
}

void BitReader::skip_bits(int n) {
    if (n <= 0) return;
    bit_pos_ += static_cast<size_t>(n);
    if (bit_pos_ > total_bits_) {
        bit_pos_ = total_bits_;
    }
}

size_t BitReader::bits_remaining() const {
    return (bit_pos_ < total_bits_) ? (total_bits_ - bit_pos_) : 0;
}

void BitReader::set_position_bits(size_t pos) {
    bit_pos_ = std::min(pos, total_bits_);
}

} // namespace audio_codecs::core
