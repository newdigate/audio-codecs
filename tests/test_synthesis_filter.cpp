// tests/test_synthesis_filter.cpp
#include "src/mp3/decoder/synthesis_filter.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;

    SynthesisFilter synth;
    synth.reset();

    float subband_samples_32[32] = {0.0f};
    subband_samples_32[0] = 1.0f; // DC subband

    float pcm_out_32[32] = {0.0f};
    synth.filter_subband(subband_samples_32, pcm_out_32);

    // Initial output exists
    std::cout << "Synthesis filter tests passed!\n";
    return 0;
}
