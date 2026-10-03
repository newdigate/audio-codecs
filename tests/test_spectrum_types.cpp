#include "audio_codecs/spectrum/spectrum_types.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace audio_codecs::spectrum;

int main() {
    static_assert(sizeof(AsvLodDescriptor) == 16, "AsvLodDescriptor must be 16 bytes");
    static_assert(sizeof(AsvHeader) == 128, "AsvHeader must be exactly 128 bytes");

    static_assert(offsetof(AsvHeader, magic) == 0, "magic offset must be 0");
    static_assert(offsetof(AsvHeader, version) == 4, "version offset must be 4");
    static_assert(offsetof(AsvHeader, flags) == 6, "flags offset must be 6");
    static_assert(offsetof(AsvHeader, sample_rate) == 8, "sample_rate offset must be 8");
    static_assert(offsetof(AsvHeader, channels) == 12, "channels offset must be 12");
    static_assert(offsetof(AsvHeader, num_bands) == 13, "num_bands offset must be 13");
    static_assert(offsetof(AsvHeader, fft_size) == 14, "fft_size offset must be 14");
    static_assert(offsetof(AsvHeader, hop_size) == 16, "hop_size offset must be 16");
    static_assert(offsetof(AsvHeader, min_freq_hz) == 18, "min_freq_hz offset must be 18");
    static_assert(offsetof(AsvHeader, max_freq_hz) == 20, "max_freq_hz offset must be 20");
    static_assert(offsetof(AsvHeader, lod_count) == 22, "lod_count offset must be 22");
    static_assert(offsetof(AsvHeader, total_pcm_frames) == 24, "total_pcm_frames offset must be 24");
    static_assert(offsetof(AsvHeader, duration_ms) == 32, "duration_ms offset must be 32");
    static_assert(offsetof(AsvHeader, lods) == 36, "lods offset must be 36");
    static_assert(offsetof(AsvHeader, reserved) == 100, "reserved offset must be 100");

    static_assert(offsetof(AsvLodDescriptor, downsample_ratio) == 0, "downsample_ratio offset must be 0");
    static_assert(offsetof(AsvLodDescriptor, frame_count) == 4, "frame_count offset must be 4");
    static_assert(offsetof(AsvLodDescriptor, file_offset) == 8, "file_offset offset must be 8");

    assert(static_cast<uint16_t>(ASV_FLAG_STEREO) == 1);
    assert(static_cast<uint16_t>(ASV_FLAG_HAS_LODS) == 2);

    AsvHeader hdr{};
    hdr.magic = ASV_MAGIC;
    hdr.version = ASV_VERSION;
    hdr.flags = ASV_FLAG_HAS_LODS;
    hdr.sample_rate = 44100;
    hdr.channels = 1;
    hdr.num_bands = DEFAULT_NUM_BANDS;
    hdr.fft_size = DEFAULT_FFT_SIZE;
    hdr.hop_size = DEFAULT_HOP_SIZE;
    hdr.min_freq_hz = 20;
    hdr.max_freq_hz = 20000;
    hdr.lod_count = 2;
    hdr.total_pcm_frames = 44100 * 60;
    hdr.duration_ms = 60000;

    assert(hdr.magic == 0x31565341);
    assert(std::memcmp(&hdr.magic, "ASV1", 4) == 0);
    assert(hdr.version == 1);
    assert(hdr.channels == 1);
    assert(hdr.num_bands == 64);
    assert(hdr.fft_size == 1024);
    assert(hdr.hop_size == 512);
    assert(sizeof(hdr) == 128);

    std::cout << "test_spectrum_types PASSED\n";
    return 0;
}

