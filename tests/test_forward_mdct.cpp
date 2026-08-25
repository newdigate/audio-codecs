// tests/test_forward_mdct.cpp
#include "src/mp3/encoder/mdct.h"
#include "src/mp3/decoder/imdct.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;

    ForwardMdct mdct;
    mdct.reset();

    float time_36[36] = {0.0f};
    for (int i = 0; i < 36; ++i) time_36[i] = 1.0f;

    float mdct_18[18] = {0.0f};
    mdct.transform_subband(time_36, mdct_18, 0); // normal window

    // Check energy
    float energy = 0.0f;
    for (int k = 0; k < 18; ++k) energy += mdct_18[k] * mdct_18[k];
    assert(energy > 0.0f);

    std::cout << "Forward MDCT tests passed!\n";
    return 0;
}
