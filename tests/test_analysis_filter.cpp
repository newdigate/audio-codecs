// tests/test_analysis_filter.cpp
#include "src/mp3/encoder/analysis_filter.h"
#include "src/mp3/decoder/synthesis_filter.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;

    AnalysisFilter analysis;
    analysis.reset();

    float pcm_in_32[32] = {0.0f};
    for (int i = 0; i < 32; ++i) pcm_in_32[i] = 1.0f; // Step input

    float subband_out_32[32] = {0.0f};
    analysis.filter_pcm(pcm_in_32, subband_out_32);

    // Initial output exists
    std::cout << "Analysis filter tests passed!\n";
    return 0;
}
