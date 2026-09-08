#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace audio_codecs::preview {

class SeekableReader {
public:
    virtual ~SeekableReader() = default;
    virtual size_t read(uint8_t* dest, size_t bytes) = 0;
    virtual bool seek(uint64_t position) = 0;
    virtual uint64_t position() const = 0;
    virtual uint64_t size() const = 0;
};

class SeekableWriter {
public:
    virtual ~SeekableWriter() = default;
    virtual size_t write(const uint8_t* src, size_t bytes) = 0;
    virtual bool seek(uint64_t position) = 0;
    virtual uint64_t position() const = 0;
    virtual uint64_t size() const = 0;
    virtual void flush() = 0;
};

class MemoryReader : public SeekableReader {
public:
    MemoryReader(const uint8_t* data, size_t size);
    size_t read(uint8_t* dest, size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    uint64_t size() const override;

private:
    const uint8_t* data_{nullptr};
    size_t size_{0};
    size_t pos_{0};
};

class MemoryWriter : public SeekableWriter, public SeekableReader {
public:
    MemoryWriter(uint8_t* buffer, size_t capacity);
    size_t write(const uint8_t* src, size_t bytes) override;
    size_t read(uint8_t* dest, size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    uint64_t size() const override;
    void flush() override {}

    const uint8_t* data() const { return buffer_; }

private:
    uint8_t* buffer_{nullptr};
    size_t capacity_{0};
    size_t pos_{0};
    size_t length_{0};
};

class FileStreamReader : public SeekableReader {
public:
    explicit FileStreamReader(std::FILE* fp);
    size_t read(uint8_t* dest, size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    uint64_t size() const override;

private:
    std::FILE* fp_{nullptr};
    uint64_t size_{0};
};

class FileStreamWriter : public SeekableWriter, public SeekableReader {
public:
    explicit FileStreamWriter(std::FILE* fp);
    size_t write(const uint8_t* src, size_t bytes) override;
    size_t read(uint8_t* dest, size_t bytes) override;
    bool seek(uint64_t position) override;
    uint64_t position() const override;
    uint64_t size() const override;
    void flush() override;

private:
    std::FILE* fp_{nullptr};
};

} // namespace audio_codecs::preview
