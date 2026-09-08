#include "audio_codecs/preview/preview_types.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>

using namespace audio_codecs::preview;

int main() {
    // Size assertions
    static_assert(sizeof(ApvLodDescriptor) == 16, "ApvLodDescriptor must be 16 bytes");
    static_assert(sizeof(ApvHeader) == 128, "ApvHeader must be exactly 128 bytes");
    static_assert(sizeof(WaveformPointMono) == 2, "WaveformPointMono must be 2 bytes");
    static_assert(sizeof(WaveformPointStereo) == 4, "WaveformPointStereo must be 4 bytes");

    // Offset assertions to ensure exact binary layout
    static_assert(offsetof(ApvHeader, magic) == 0, "magic offset must be 0");
    static_assert(offsetof(ApvHeader, version) == 4, "version offset must be 4");
    static_assert(offsetof(ApvHeader, flags) == 6, "flags offset must be 6");
    static_assert(offsetof(ApvHeader, sample_rate) == 8, "sample_rate offset must be 8");
    static_assert(offsetof(ApvHeader, channels) == 12, "channels offset must be 12");
    static_assert(offsetof(ApvHeader, bytes_per_chunk) == 13, "bytes_per_chunk offset must be 13");
    static_assert(offsetof(ApvHeader, samples_per_base_chunk) == 14, "samples_per_base_chunk offset must be 14");
    static_assert(offsetof(ApvHeader, total_pcm_frames) == 16, "total_pcm_frames offset must be 16");
    static_assert(offsetof(ApvHeader, duration_ms) == 24, "duration_ms offset must be 24");
    static_assert(offsetof(ApvHeader, source_file_size) == 28, "source_file_size offset must be 28");
    static_assert(offsetof(ApvHeader, source_header_crc32) == 32, "source_header_crc32 offset must be 32");
    static_assert(offsetof(ApvHeader, lod_count) == 36, "lod_count offset must be 36");
    static_assert(offsetof(ApvHeader, reserved) == 37, "reserved offset must be 37");
    static_assert(offsetof(ApvHeader, lods) == 48, "lods offset must be 48 (8-byte aligned)");
    static_assert(offsetof(ApvHeader, padding) == 112, "padding offset must be 112");

    static_assert(offsetof(ApvLodDescriptor, downsample_ratio) == 0);
    static_assert(offsetof(ApvLodDescriptor, chunk_count) == 4);
    static_assert(offsetof(ApvLodDescriptor, file_offset) == 8);

    static_assert(offsetof(WaveformPointMono, min) == 0);
    static_assert(offsetof(WaveformPointMono, max) == 1);

    static_assert(offsetof(WaveformPointStereo, left_min) == 0);
    static_assert(offsetof(WaveformPointStereo, left_max) == 1);
    static_assert(offsetof(WaveformPointStereo, right_min) == 2);
    static_assert(offsetof(WaveformPointStereo, right_max) == 3);

    // Flag constants
    assert(static_cast<uint16_t>(APV_FLAG_STEREO) == 1);
    assert(static_cast<uint16_t>(APV_FLAG_HAS_LODS) == 2);

    // Default values
    ApvHeader default_hdr;
    assert(default_hdr.magic == APV_MAGIC);
    assert(default_hdr.version == APV_VERSION);
    assert(default_hdr.sample_rate == 44100);
    assert(default_hdr.channels == 2);
    assert(default_hdr.bytes_per_chunk == 4);
    assert(default_hdr.samples_per_base_chunk == BASE_CHUNK_FRAMES);
    assert(default_hdr.total_pcm_frames == 0);
    assert(default_hdr.duration_ms == 0);
    assert(default_hdr.source_file_size == 0);
    assert(default_hdr.source_header_crc32 == 0);
    assert(default_hdr.lod_count == 0);

    ApvLodDescriptor default_lod;
    assert(default_lod.downsample_ratio == 1);
    assert(default_lod.chunk_count == 0);
    assert(default_lod.file_offset == 0);

    WaveformPointMono mono_pt{};
    assert(mono_pt.min == 0);
    assert(mono_pt.max == 0);

    WaveformPointStereo stereo_pt{};
    assert(stereo_pt.left_min == 0);
    assert(stereo_pt.left_max == 0);
    assert(stereo_pt.right_min == 0);
    assert(stereo_pt.right_max == 0);

    // Populated header test from task brief
    ApvHeader hdr{};
    hdr.magic = APV_MAGIC;
    hdr.version = APV_VERSION;
    hdr.flags = APV_FLAG_STEREO | APV_FLAG_HAS_LODS;
    hdr.sample_rate = 44100;
    hdr.channels = 2;
    hdr.bytes_per_chunk = 4;
    hdr.samples_per_base_chunk = BASE_CHUNK_FRAMES;
    hdr.total_pcm_frames = 44100 * 60;
    hdr.duration_ms = 60000;
    hdr.lod_count = 3;

    assert(hdr.magic == 0x31565041);
    assert(hdr.version == 1);
    assert(hdr.channels == 2);
    assert(hdr.bytes_per_chunk == 4);
    assert(hdr.samples_per_base_chunk == 128);
    assert(sizeof(hdr) == 128);

    std::cout << "test_preview_types PASSED\n";
    return 0;
}
