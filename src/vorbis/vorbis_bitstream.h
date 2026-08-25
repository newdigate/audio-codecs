#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace audio_codecs::vorbis {

class VorbisBitReader {
public:
    VorbisBitReader() = default;
    VorbisBitReader(const uint8_t* data, size_t num_bytes) {
        init(data, num_bytes);
    }

    void init(const uint8_t* data, size_t num_bytes) {
        data_ = data;
        total_bits_ = num_bytes * 8;
        bit_pos_ = 0;
    }

    // Read up to 32 bits (LSB first per Vorbis spec)
    uint32_t read_bits(int n) {
        if (n <= 0 || n > 32 || bit_pos_ >= total_bits_) return 0;
        
        int bits_to_read = std::min(n, static_cast<int>(total_bits_ - bit_pos_));
        uint32_t result = 0;
        int bits_read = 0;

        while (bits_read < bits_to_read) {
            size_t byte_idx = bit_pos_ >> 3;
            size_t bit_off = bit_pos_ & 7;
            int take = std::min(8 - static_cast<int>(bit_off), bits_to_read - bits_read);
            uint32_t mask = (take == 32) ? 0xFFFFFFFFU : ((1U << take) - 1);
            uint32_t part = (data_[byte_idx] >> bit_off) & mask;

            result |= (part << bits_read);
            bit_pos_ += take;
            bits_read += take;
        }

        return result;
    }

    bool read_bits(int n, uint32_t& out_val) {
        if (n < 0 || n > 32 || bit_pos_ + static_cast<size_t>(n) > total_bits_) {
            out_val = 0;
            return false;
        }
        out_val = read_bits(n);
        return true;
    }

    uint32_t peek_bits(int n) {
        size_t saved_pos = bit_pos_;
        uint32_t val = read_bits(n);
        bit_pos_ = saved_pos;
        return val;
    }

    void skip_bits(int n) {
        if (n > 0) {
            bit_pos_ = std::min(bit_pos_ + static_cast<size_t>(n), total_bits_);
        }
    }

    size_t bits_remaining() const {
        return (bit_pos_ < total_bits_) ? (total_bits_ - bit_pos_) : 0;
    }

    size_t get_position_bits() const { return bit_pos_; }
    void set_position_bits(size_t pos) { bit_pos_ = std::min(pos, total_bits_); }

private:
    const uint8_t* data_{nullptr};
    size_t total_bits_{0};
    size_t bit_pos_{0};
};

class VorbisBitWriter {
public:
    VorbisBitWriter() = default;
    VorbisBitWriter(uint8_t* buffer, size_t max_bytes) {
        init(buffer, max_bytes);
    }

    void init(uint8_t* buffer, size_t max_bytes) {
        buffer_ = buffer;
        max_bits_ = max_bytes * 8;
        bit_pos_ = 0;
        if (buffer_ && max_bytes > 0) {
            std::memset(buffer_, 0, max_bytes);
        }
    }

    // Write up to 32 bits (LSB first per Vorbis spec)
    void write_bits(uint32_t value, int n) {
        if (!buffer_ || n <= 0 || n > 32) return;
        int bits_to_write = std::min(n, static_cast<int>(max_bits_ - bit_pos_));
        int bits_written = 0;

        while (bits_written < bits_to_write) {
            size_t byte_idx = bit_pos_ >> 3;
            size_t bit_off = bit_pos_ & 7;
            int take = std::min(8 - static_cast<int>(bit_off), bits_to_write - bits_written);
            uint32_t mask = (take == 32) ? 0xFFFFFFFFU : ((1U << take) - 1);
            uint32_t part = (value >> bits_written) & mask;

            buffer_[byte_idx] |= static_cast<uint8_t>(part << bit_off);
            bit_pos_ += take;
            bits_written += take;
        }
    }

    void flush() {
        // Zero-padding is already preserved since buffer is initialized to 0
    }

    size_t get_bit_count() const { return bit_pos_; }
    size_t bytes_written() const { return (bit_pos_ + 7) >> 3; }
    uint8_t* data() { return buffer_; }
    const uint8_t* data() const { return buffer_; }

private:
    uint8_t* buffer_{nullptr};
    size_t max_bits_{0};
    size_t bit_pos_{0};
};

} // namespace audio_codecs::vorbis
