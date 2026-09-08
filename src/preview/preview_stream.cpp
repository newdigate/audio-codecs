#include "audio_codecs/preview/preview_stream.h"
#include <algorithm>

namespace audio_codecs::preview {

// --- MemoryReader ---
MemoryReader::MemoryReader(const uint8_t* data, size_t size)
    : data_(data), size_(size), pos_(0) {}

size_t MemoryReader::read(uint8_t* dest, size_t bytes) {
    if (!data_ || pos_ >= size_ || bytes == 0) return 0;
    size_t to_read = std::min(bytes, size_ - pos_);
    std::memcpy(dest, data_ + pos_, to_read);
    pos_ += to_read;
    return to_read;
}

bool MemoryReader::seek(uint64_t position) {
    if (position > size_) return false;
    pos_ = static_cast<size_t>(position);
    return true;
}

uint64_t MemoryReader::position() const { return pos_; }
uint64_t MemoryReader::size() const { return size_; }

// --- MemoryWriter ---
MemoryWriter::MemoryWriter(uint8_t* buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity), pos_(0), length_(0) {}

size_t MemoryWriter::write(const uint8_t* src, size_t bytes) {
    if (!buffer_ || pos_ >= capacity_ || bytes == 0) return 0;
    size_t to_write = std::min(bytes, capacity_ - pos_);
    std::memcpy(buffer_ + pos_, src, to_write);
    pos_ += to_write;
    if (pos_ > length_) length_ = pos_;
    return to_write;
}

size_t MemoryWriter::read(uint8_t* dest, size_t bytes) {
    if (!buffer_ || pos_ >= length_ || bytes == 0) return 0;
    size_t to_read = std::min(bytes, length_ - pos_);
    std::memcpy(dest, buffer_ + pos_, to_read);
    pos_ += to_read;
    return to_read;
}

bool MemoryWriter::seek(uint64_t position) {
    if (position > capacity_) return false;
    pos_ = static_cast<size_t>(position);
    return true;
}

uint64_t MemoryWriter::position() const { return pos_; }
uint64_t MemoryWriter::size() const { return length_; }

// --- FileStreamReader ---
FileStreamReader::FileStreamReader(std::FILE* fp) : fp_(fp) {
    if (fp_) {
        std::fseek(fp_, 0, SEEK_END);
        long sz = std::ftell(fp_);
        size_ = (sz < 0) ? 0 : static_cast<uint64_t>(sz);
        std::fseek(fp_, 0, SEEK_SET);
    }
}

size_t FileStreamReader::read(uint8_t* dest, size_t bytes) {
    if (!fp_) return 0;
    return std::fread(dest, 1, bytes, fp_);
}

bool FileStreamReader::seek(uint64_t position) {
    if (!fp_) return false;
    return std::fseek(fp_, static_cast<long>(position), SEEK_SET) == 0;
}

uint64_t FileStreamReader::position() const {
    if (!fp_) return 0;
    long pos = std::ftell(fp_);
    return (pos < 0) ? 0 : static_cast<uint64_t>(pos);
}

uint64_t FileStreamReader::size() const { return size_; }

// --- FileStreamWriter ---
FileStreamWriter::FileStreamWriter(std::FILE* fp) : fp_(fp) {}

size_t FileStreamWriter::write(const uint8_t* src, size_t bytes) {
    if (!fp_) return 0;
    return std::fwrite(src, 1, bytes, fp_);
}

size_t FileStreamWriter::read(uint8_t* dest, size_t bytes) {
    if (!fp_) return 0;
    return std::fread(dest, 1, bytes, fp_);
}

bool FileStreamWriter::seek(uint64_t position) {
    if (!fp_) return false;
    return std::fseek(fp_, static_cast<long>(position), SEEK_SET) == 0;
}

uint64_t FileStreamWriter::position() const {
    if (!fp_) return 0;
    long pos = std::ftell(fp_);
    return (pos < 0) ? 0 : static_cast<uint64_t>(pos);
}

uint64_t FileStreamWriter::size() const {
    if (!fp_) return 0;
    long curr = std::ftell(fp_);
    if (curr < 0) return 0;
    if (std::fseek(fp_, 0, SEEK_END) != 0) return 0;
    long sz = std::ftell(fp_);
    std::fseek(fp_, curr, SEEK_SET);
    return (sz < 0) ? 0 : static_cast<uint64_t>(sz);
}

void FileStreamWriter::flush() {
    if (fp_) std::fflush(fp_);
}

} // namespace audio_codecs::preview
