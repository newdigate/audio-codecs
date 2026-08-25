// tests/test_core_types.cpp
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/core/encoder_interface.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <iostream>
#include <cmath>

int main() {
    using namespace audio_codecs;
    AudioConfig config{44100, 2, 128, false, 4};
    assert(config.sample_rate == 44100);
    assert(config.channels == 2);
    assert(config.bitrate_kbps == 128);
    assert(!config.vbr);

    float dummy_buffer[10] = {0.0f};
    PcmView<float> view{dummy_buffer, 5, 2, true};
    assert(view.samples_per_channel == 5);
    assert(view.channels == 2);

    assert(std::fabs(constants::PI - 3.14159265358979323846f) < 1e-6f);
    assert(std::fabs(constants::SQRT2 - 1.41421356237309504880f) < 1e-6f);
    assert(std::fabs(constants::INV_SQRT2 - 0.70710678118654752440f) < 1e-6f);

    std::cout << "Core types test passed!\n";
    return 0;
}
