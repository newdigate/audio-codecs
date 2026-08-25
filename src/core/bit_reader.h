#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs::core {

class BitReader {
public:
    BitReader() = default;

    void init(const uint8_t* data, size_t num_bytes);
    
    // Read up to 32 bits (MSB first)
    uint32_t read_bits(int n);
    
    // Peek up to 32 bits without advancing position
    uint32_t peek_bits(int n);
    
    // Skip n bits
    void skip_bits(int n);
    
    // Total bits remaining
    size_t bits_remaining() const;
    
    // Current bit position
    size_t get_position_bits() const { return bit_pos_; }
    
    // Set bit position
    void set_position_bits(size_t pos);

private:
    const uint8_t* data_{nullptr};
    size_t total_bits_{0};
    size_t bit_pos_{0};
};

} // namespace audio_codecs::core
