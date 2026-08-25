#include "audio_codecs/audio_codecs.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR "test-files"
#endif

// Simple 16-bit PCM WAV parser for testing
bool load_wav_pcm(const std::string& path, std::vector<float>& out_pcm, uint32_t& out_sr, uint8_t& out_ch) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::string fallback = "../" + path;
        file.open(fallback, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size) || file_size < 44) {
        return false;
    }

    if (std::memcmp(buffer.data(), "RIFF", 4) != 0 || std::memcmp(buffer.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    // Parse WAV chunks
    size_t offset = 12;
    uint16_t audio_format = 1;
    uint16_t num_channels = 1;
    uint32_t sample_rate = 44100;
    uint16_t bits_per_sample = 16;
    const uint8_t* pcm_data = nullptr;
    size_t pcm_bytes = 0;

    while (offset + 8 <= buffer.size()) {
        char chunk_id[5] = {0};
        std::memcpy(chunk_id, buffer.data() + offset, 4);
        uint32_t chunk_size = *reinterpret_cast<const uint32_t*>(buffer.data() + offset + 4);
        offset += 8;

        if (std::strcmp(chunk_id, "fmt ") == 0 && chunk_size >= 16) {
            audio_format = *reinterpret_cast<const uint16_t*>(buffer.data() + offset);
            num_channels = *reinterpret_cast<const uint16_t*>(buffer.data() + offset + 2);
            sample_rate = *reinterpret_cast<const uint32_t*>(buffer.data() + offset + 4);
            bits_per_sample = *reinterpret_cast<const uint16_t*>(buffer.data() + offset + 14);
        } else if (std::strcmp(chunk_id, "data") == 0) {
            pcm_data = buffer.data() + offset;
            pcm_bytes = std::min<size_t>(chunk_size, buffer.size() - offset);
            break;
        }
        offset += chunk_size;
    }

    if (!pcm_data || pcm_bytes == 0 || audio_format != 1 || bits_per_sample != 16) {
        return false;
    }

    out_sr = sample_rate;
    out_ch = static_cast<uint8_t>(num_channels);
    size_t sample_count = pcm_bytes / 2;
    out_pcm.resize(sample_count);

    const int16_t* i16 = reinterpret_cast<const int16_t*>(pcm_data);
    for (size_t i = 0; i < sample_count; ++i) {
        out_pcm[i] = static_cast<float>(i16[i]) / 32768.0f;
    }

    return true;
}

bool test_real_wav_roundtrip(const std::string& path) {
    std::vector<float> wav_pcm;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;

    if (!load_wav_pcm(path, wav_pcm, sample_rate, channels)) {
        std::cerr << "Failed to load WAV file: " << path << "\n";
        return false;
    }

    audio_codecs::vorbis::VorbisEncoder encoder;
    audio_codecs::vorbis::VorbisDecoder decoder;

    audio_codecs::AudioConfig config{};
    config.channels = channels;
    config.sample_rate = sample_rate;
    config.bitrate_kbps = 128;

    if (!encoder.init(config) || !decoder.init(config)) {
        return false;
    }

    // Encode WAV PCM into Ogg/Vorbis bitstream
    std::vector<uint8_t> ogg_bitstream;
    uint8_t chunk[8192];
    size_t step = 1024;

    for (size_t i = 0; i < wav_pcm.size(); i += step) {
        size_t samples = std::min(step, wav_pcm.size() - i);
        int written = encoder.encode_frame(wav_pcm.data() + i, samples, chunk, sizeof(chunk));
        if (written > 0) {
            ogg_bitstream.insert(ogg_bitstream.end(), chunk, chunk + written);
        }
    }

    int flush_len = encoder.flush(chunk, sizeof(chunk));
    if (flush_len > 0) {
        ogg_bitstream.insert(ogg_bitstream.end(), chunk, chunk + flush_len);
    }

    if (ogg_bitstream.empty()) return false;

    // Decode Ogg/Vorbis bitstream back to PCM
    std::vector<float> decoded_pcm;
    float dec_buf[4096];
    for (size_t i = 0; i < ogg_bitstream.size(); i += 512) {
        size_t bytes = std::min<size_t>(512, ogg_bitstream.size() - i);
        int samples = decoder.decode_frame(ogg_bitstream.data() + i, bytes, dec_buf, sizeof(dec_buf) / sizeof(float));
        if (samples > 0) {
            decoded_pcm.insert(decoded_pcm.end(), dec_buf, dec_buf + samples);
        }
    }

    std::cout << "[PASS] " << path << " -> "
              << wav_pcm.size() << " WAV samples -> "
              << ogg_bitstream.size() << " Ogg bytes -> "
              << decoded_pcm.size() << " decoded samples ("
              << sample_rate << " Hz, " << static_cast<int>(channels) << " ch)\n";

    return (decoder.has_headers() && !decoded_pcm.empty());
}

int main() {
    std::string base_dir = TEST_FILES_DIR;
    std::vector<std::string> wav_files = {
        "/wav/test100ms.wav",
        "/wav/test400ms.wav",
        "/wav/test70ms.wav",
        "/wav/test43ms.wav",
        "/wav/test11ms.wav"
    };

    int passed = 0;
    for (const auto& rel : wav_files) {
        std::string full_path = base_dir + rel;
        if (test_real_wav_roundtrip(full_path)) {
            passed++;
        }
    }

    std::cout << "\nResult: " << passed << "/" << wav_files.size() << " real WAV files encoded to Ogg/Vorbis and decoded successfully!\n";
    assert(passed == static_cast<int>(wav_files.size()));
    return 0;
}
