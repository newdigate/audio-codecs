#include "src/vorbis/vorbis_common.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::vorbis;
    // Test ilog
    assert(vorbis_ilog(0) == 0);
    assert(vorbis_ilog(1) == 1);
    assert(vorbis_ilog(2) == 2);
    assert(vorbis_ilog(7) == 3);
    assert(vorbis_ilog(8) == 4);

    // Test float32 unpacking
    float zero = vorbis_unpack_float32(0);
    assert(std::fabs(zero) < 1e-9f);

    // Test roundtrip packing/unpacking
    float test_vals[] = {1.0f, -1.0f, 0.5f, 128.0f, 0.001f, -42.5f};
    for (float v : test_vals) {
        uint32_t packed = vorbis_pack_float32(v);
        float unpacked = vorbis_unpack_float32(packed);
        float rel_err = std::fabs(unpacked - v) / std::fabs(v);
        assert(rel_err < 0.01f);
    }

    // Test window generation
    float win[256];
    vorbis_generate_window(win, 256);
    assert(win[0] >= 0.0f && win[255] >= 0.0f);
    // Symmetry & partition-of-unity check
    for (int i = 0; i < 128; ++i) {
        float diff = std::fabs(win[i] - win[255 - i]);
        assert(diff < 1e-4f);
        float sum_sq = win[i] * win[i] + win[i + 128] * win[i + 128];
        assert(std::fabs(sum_sq - 1.0f) < 1e-4f);
    }

    // Test slope window generation
    float slope_win[512];
    vorbis_generate_slope_window(slope_win, 512, 128, 512);
    assert(slope_win[0] == 0.0f);
    assert(slope_win[511] >= 0.0f);

    std::cout << "Vorbis common math & float32 tests passed!\n";
    return 0;
}
