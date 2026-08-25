// tests/test_requantizer.cpp
#include "src/mp3/decoder/requantizer.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;
    using namespace audio_codecs::constants;

    // Test 1: Requantize single long block value
    int16_t is[576] = {0};
    is[0] = 8; // is = 8 -> 8^(4/3) = 16.0

    ScalefactorData sf;
    sf.l[0] = 0; // sf = 0

    GranuleChannelInfo gi;
    gi.global_gain = 210; // 2^(0.25 * (210 - 210)) = 2^0 = 1.0
    gi.scalefac_scale = false; // multiplier = 0.5
    gi.preflag = false;
    gi.window_switching_flag = false;
    gi.block_type = 0;

    FrameHeader header;
    header.version = MpegVersion::Mpeg1;
    header.sample_rate = 44100;

    float xr[576] = {0.0f};
    Requantizer::requantize_granule(is, sf, gi, header, xr);

    // Expected: sign(8) * 8^(4/3) * 1.0 * 2^(-0.5 * 0) = 16.0f
    assert(std::fabs(xr[0] - 16.0f) < 1e-4f);
    assert(xr[1] == 0.0f);

    // Test 2: MS Stereo processing
    float xr_left[576] = {0.0f};
    float xr_right[576] = {0.0f};
    xr_left[0] = 10.0f;  // Mid
    xr_right[0] = 4.0f;  // Side

    header.mode = MpegMode::JointStereo;
    header.ms_stereo = true;
    header.intensity_stereo = false;

    GranuleChannelInfo gi_left = gi;
    GranuleChannelInfo gi_right = gi;

    Requantizer::process_stereo(xr_left, xr_right, gi_left, gi_right, header);

    // L = (10 + 4) * INV_SQRT2 = 14 * 0.70710678 = 9.8994949
    // R = (10 - 4) * INV_SQRT2 = 6 * 0.70710678 = 4.2426407
    assert(std::fabs(xr_left[0] - (14.0f * INV_SQRT2)) < 1e-4f);
    assert(std::fabs(xr_right[0] - (6.0f * INV_SQRT2)) < 1e-4f);

    // Test 3: Alias reduction
    float xr_alias[576] = {0.0f};
    xr_alias[17] = 1.0f; // line 18*1 - 1 - 0
    xr_alias[18] = 2.0f; // line 18*1 + 0

    Requantizer::alias_reduction(xr_alias, gi);
    // xar[17] = xr[17]*Cs[0] - xr[18]*Ca[0]
    // xar[18] = xr[18]*Cs[0] + xr[17]*Ca[0]
    float expected_17 = 1.0f * ALIAS_CS[0] - 2.0f * ALIAS_CA[0];
    float expected_18 = 2.0f * ALIAS_CS[0] + 1.0f * ALIAS_CA[0];
    assert(std::fabs(xr_alias[17] - expected_17) < 1e-4f);
    assert(std::fabs(xr_alias[18] - expected_18) < 1e-4f);

    std::cout << "Requantizer tests passed!\n";
    return 0;
}
