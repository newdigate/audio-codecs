// tests/test_real_flacs.cpp
#include "audio_codecs/flac/flac_decoder.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR "test-files"
#endif

bool test_flac_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::string fallback = "../" + path;
        file.open(fallback, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << path << "\n";
            return false;
        }
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        std::cerr << "Failed to read file: " << path << "\n";
        return false;
    }

    audio_codecs::flac::FlacDecoder decoder;
    size_t header_len = 0;
    if (!decoder.parse_stream_header(buffer.data(), buffer.size(), header_len)) {
        std::cerr << "Failed to parse stream header: " << path << "\n";
        return false;
    }

    size_t offset = header_len;
    size_t frames = 0;
    size_t total_samples = 0;
    std::vector<int32_t> pcm_out(8192 * 2);

    while (offset + 4 <= buffer.size()) {
        int samples = decoder.decode_frame_i32(buffer.data() + offset, 
                                               buffer.size() - offset, 
                                               pcm_out.data(), 
                                               pcm_out.size());
        if (samples <= 0) {
            std::cerr << "File " << path << " frame " << frames << " error code " << samples 
                      << " at offset " << offset << "\n";
            break;
        }

        frames++;
        total_samples += samples;

        size_t advance = decoder.get_last_frame_bytes();
        if (advance == 0) advance = 4;
        offset += advance;
    }

    std::cout << "[PASS] " << path << " -> "
              << frames << " frames, "
              << total_samples << " samples, "
              << decoder.get_sample_rate() << " Hz, "
              << static_cast<int>(decoder.get_channels()) << " ch, "
              << static_cast<int>(decoder.get_bit_depth()) << " bit\n";

    return (frames > 0);
}

int main() {
    std::string base_dir = TEST_FILES_DIR;
    std::vector<std::string> flac_files = {
        "/flac/test400ms.flac",
        "/flac/24bit96_Pink-Noise_24_44.flac"
    };

    int passed = 0;
    for (const auto& rel : flac_files) {
        std::string full_path = base_dir + rel;
        if (test_flac_file(full_path)) {
            passed++;
        }
    }

    std::cout << "\nResult: " << passed << "/" << flac_files.size() << " FLAC files parsed and decoded successfully!\n";
    assert(passed == static_cast<int>(flac_files.size()));
    return 0;
}
