#include "audio_codecs/preview/preview_stream.h"
#include "audio_codecs/preview/teensy_stream_adapter.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace audio_codecs::preview;

// Mock Teensy File class
struct MockTeensyFile {
    uint8_t buffer[1024]{0};
    uint64_t pos{0};
    uint64_t len{0};

    size_t read(uint8_t* dest, size_t bytes) {
        size_t available = (pos < len) ? static_cast<size_t>(len - pos) : 0;
        size_t to_read = (bytes < available) ? bytes : available;
        if (to_read > 0) {
            std::memcpy(dest, buffer + pos, to_read);
            pos += to_read;
        }
        return to_read;
    }

    size_t write(const uint8_t* src, size_t bytes) {
        size_t available = (pos < sizeof(buffer)) ? static_cast<size_t>(sizeof(buffer) - pos) : 0;
        size_t to_write = (bytes < available) ? bytes : available;
        if (to_write > 0) {
            std::memcpy(buffer + pos, src, to_write);
            pos += to_write;
            if (pos > len) len = pos;
        }
        return to_write;
    }

    bool seek(uint64_t p) {
        if (p <= len) { pos = p; return true; }
        return false;
    }

    uint64_t position() const { return pos; }
    uint64_t size() const { return len; }
    void flush() {}
};

int main() {
    // 1. MemoryWriter and MemoryReader
    uint8_t mem[512]{0};
    MemoryWriter mw(mem, sizeof(mem));
    const uint8_t data[] = "HelloAPVStream!";
    size_t written = mw.write(data, sizeof(data));
    assert(written == sizeof(data));
    assert(mw.position() == sizeof(data));
    assert(mw.size() == sizeof(data));

    MemoryReader mr(mem, mw.size());
    assert(mr.size() == sizeof(data));
    uint8_t read_buf[32]{0};
    size_t r = mr.read(read_buf, sizeof(data));
    assert(r == sizeof(data));
    assert(std::strcmp(reinterpret_cast<char*>(read_buf), "HelloAPVStream!") == 0);

    // Seek test
    assert(mr.seek(5));
    assert(mr.position() == 5);
    r = mr.read(read_buf, 3);
    assert(r == 3);
    assert(std::memcmp(read_buf, "APV", 3) == 0);

    // MemoryWriter seek and read-back test (validates SeekableReader capability on MemoryWriter)
    assert(mw.seek(0));
    assert(mw.position() == 0);
    assert(mw.size() == sizeof(data)); // Seeking backwards must not truncate size!
    SeekableReader* mw_reader = dynamic_cast<SeekableReader*>(&mw);
    assert(mw_reader != nullptr);
    std::memset(read_buf, 0, sizeof(read_buf));
    r = mw_reader->read(read_buf, sizeof(data));
    assert(r == sizeof(data));
    assert(std::strcmp(reinterpret_cast<char*>(read_buf), "HelloAPVStream!") == 0);

    // MemoryWriter seek out-of-bounds
    assert(!mw.seek(sizeof(mem) + 1));

    // 2. TeensyFileStream adapter test
    MockTeensyFile mock_file;
    TeensyFileStream<MockTeensyFile> stream(mock_file);
    written = stream.write(data, sizeof(data));
    assert(written == sizeof(data));
    assert(stream.position() == sizeof(data));
    assert(stream.seek(0));
    std::memset(read_buf, 0, sizeof(read_buf));
    r = stream.read(read_buf, sizeof(data));
    assert(r == sizeof(data));
    assert(std::strcmp(reinterpret_cast<char*>(read_buf), "HelloAPVStream!") == 0);

    // 3. FileStreamReader and FileStreamWriter test
    std::FILE* tmp = std::tmpfile();
    assert(tmp != nullptr);
    {
        FileStreamWriter fsw(tmp);
        written = fsw.write(data, sizeof(data));
        assert(written == sizeof(data));
        assert(fsw.position() == sizeof(data));
        assert(fsw.size() == sizeof(data));
        fsw.flush();

        // Check seek on FileStreamWriter
        assert(fsw.seek(0));
        assert(fsw.position() == 0);
        assert(fsw.size() == sizeof(data));

        // FileStreamReader test on same FILE*
        FileStreamReader fsr(tmp);
        assert(fsr.size() == sizeof(data));
        assert(fsr.seek(0));
        std::memset(read_buf, 0, sizeof(read_buf));
        r = fsr.read(read_buf, sizeof(data));
        assert(r == sizeof(data));
        assert(std::strcmp(reinterpret_cast<char*>(read_buf), "HelloAPVStream!") == 0);
    }
    std::fclose(tmp);

    std::cout << "test_preview_stream PASSED\n";
    return 0;
}
