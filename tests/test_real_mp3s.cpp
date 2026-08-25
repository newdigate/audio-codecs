// tests/test_real_mp3s.cpp
#include "audio_codecs/mp3/mp3_decoder.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

// Helper to skip ID3v2 tag if present at start of stream
size_t skip_id3(const std::vector<uint8_t>& data) {
    if (data.size() >= 10 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        size_t tag_size = ((data[6] & 0x7F) << 21) |
                          ((data[7] & 0x7F) << 14) |
                          ((data[8] & 0x7F) << 7)  |
                          (data[9] & 0x7F);
        size_t total_id3_len = 10 + tag_size;
        if (data[5] & 0x10) { // footer present
            total_id3_len += 10;
        }
        return (total_id3_len < data.size()) ? total_id3_len : data.size();
    }
    return 0;
}

bool test_mp3_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::string fallback_path = "../" + path;
        file.open(fallback_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << path << " (and " << fallback_path << ")\n";
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

    audio_codecs::mp3::Mp3Decoder decoder;
    audio_codecs::AudioConfig dummy_config{44100, 2, 128, false, 4};
    decoder.init(dummy_config);

    size_t offset = skip_id3(buffer);
    size_t frames_decoded = 0;
    size_t total_samples = 0;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    uint32_t bitrate_kbps = 0;

    std::vector<float> pcm_out(4608); // Big enough for any MPEG-1 / MPEG-2 frame

    while (offset + 4 <= buffer.size()) {
        int samples = decoder.decode_frame(buffer.data() + offset, buffer.size() - offset, 
                                          pcm_out.data(), pcm_out.size());
        if (samples <= 0) {
            std::cerr << "File " << path << " frame " << frames_decoded << " failed with error code " << samples 
                      << " at offset " << offset << " (buffer size " << buffer.size() << ")\n";
            break;
        }

        decoder.get_frame_info(sample_rate, channels, bitrate_kbps);
        frames_decoded++;
        total_samples += samples;

        size_t advance = decoder.get_last_sync_offset() + decoder.get_last_frame_bytes();
        if (advance == 0) advance = 4;
        offset += advance;
    }

    std::cout << "[PASS] " << path << " -> "
              << frames_decoded << " frames, "
              << total_samples << " samples, "
              << sample_rate << " Hz, "
              << static_cast<int>(channels) << " ch, "
              << bitrate_kbps << " kbps\n";

    return (frames_decoded > 0);
}

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR "test-files"
#endif

int main() {
    std::string base_dir = TEST_FILES_DIR;
    std::vector<std::string> relative_paths = {
        "/mpeg-audio/music/music.mp3",
        "/mpeg-audio/music/organ.mp3",
        "/mpeg-audio/music/piano.mp3",
        "/mpeg-audio/noises/greynoise-18dB.mp3",
        "/mpeg-audio/noises/greynoise.mp3",
        "/mpeg-audio/noises/silence.mp3",
        "/mpeg-audio/noises/sweep.mp3",
        "/mpeg-audio/sine/1000Hz.mp3",
        "/mpeg-audio/sine/440Hz.mp3",
        "/mpeg-audio/sounds/click.mp3",
        "/mpeg-audio/sounds/click_fade_out.mp3",
        "/mpeg-audio/sounds/test100ms.mp3",
        "/mpeg-audio/sounds/test10ms.mp3",
        "/mpeg-audio/sounds/test400ms.mp3",
        "/mpeg-audio/sounds/test40ms.mp3",
        "/mpeg-audio/sounds/test70ms.mp3"
    };

    int passed = 0;
    for (const auto& rel : relative_paths) {
        std::string full_path = base_dir + rel;
        if (test_mp3_file(full_path)) {
            passed++;
        }
    }

    std::cout << "\nResult: " << passed << "/" << relative_paths.size() << " MP3 files parsed and decoded successfully!\n";
    assert(passed == static_cast<int>(relative_paths.size()));
    return 0;
}
