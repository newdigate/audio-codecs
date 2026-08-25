// tests/test_flac_decoder.cpp
#include "audio_codecs/flac/flac_decoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    FlacDecoder decoder;
    audio_codecs::AudioConfig config{44100, 2, 0, false, 2};
    assert(decoder.init(config));

    assert(decoder.get_sample_rate() == 44100);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_bit_depth() == 16);

    std::cout << "FLAC Decoder facade test initialized successfully!\n";
    return 0;
}
