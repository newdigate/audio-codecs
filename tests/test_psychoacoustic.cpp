// tests/test_psychoacoustic.cpp
#include "src/mp3/encoder/psychoacoustic.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;

    PsychoacousticModel psycho;
    psycho.init(44100);

    float pcm_1024[1024] = {0.0f};
    // Generate a 1 kHz sine wave at 44.1 kHz
    for (int i = 0; i < 1024; ++i) {
        pcm_1024[i] = std::sin(audio_codecs::constants::TWO_PI * 1000.0f * i / 44100.0f);
    }

    float mask_thresholds[22] = {0.0f};
    float smr[22] = {0.0f};

    psycho.calculate_masking(pcm_1024, mask_thresholds, smr);

    // Band around 1 kHz (sfb ~ 5) should have peak SMR and masking threshold
    float max_smr = -100.0f;
    int max_sfb = 0;
    for (int s = 0; s < 22; ++s) {
        if (smr[s] > max_smr) {
            max_smr = smr[s];
            max_sfb = s;
        }
    }
    assert(max_sfb >= 3 && max_sfb <= 8);
    assert(max_smr > 10.0f); // Positive SMR for 1 kHz tone

    std::cout << "Psychoacoustic model tests passed!\n";
    return 0;
}
