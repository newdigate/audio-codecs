// tests/test_imdct.cpp
#include "src/mp3/decoder/imdct.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;

    ImdctEngine imdct;
    imdct.reset();

    // Test 1: Transform DC / constant value on long block
    float xr_18[18] = {0.0f};
    xr_18[0] = 1.0f; // Only DC frequency line

    float out_18[18] = {0.0f};
    imdct.transform_subband(xr_18, 0, 0, out_18); // ch 0, sb 0

    // After first frame (starts with 0 overlap), out_18 has windowed half
    // Non-zero output
    float energy = 0.0f;
    for (int i = 0; i < 18; ++i) energy += out_18[i] * out_18[i];
    assert(energy > 0.0f);

    std::cout << "IMDCT tests passed!\n";
    return 0;
}
