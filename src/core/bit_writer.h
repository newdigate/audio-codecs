#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::core {

class BitWriter {
public:
    BitWriter() = default;

    void init(uint8_t* buffer, size_t max_bytes);
    
    // Write up to 32 bits (MSB first)
    void write_bits(uint32_t value, int n);
    
    // Flush remaining partial byte with zero padding
    void flush_to_byte();
    
    // Total bits written so far
    size_t get_bit_count() const { return bit_pos_; }
    
    // Total full/partial bytes touched
    size_t get_byte_count() const { return (bit_pos_ + 7) >> 3; }

    // Pointer to underlying buffer
    uint8_t* data() { return buffer_; }
    const uint8_t* data() const { return buffer_; }

private:
    uint8_t* buffer_{nullptr};
    size_t max_bits_{0};
    size_t bit_pos_{0};
};

} // namespace audio_codecs::core
