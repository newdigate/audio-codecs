// tests/test_flac_decorrelator.cpp
#include "src/flac/decoder/channel_decorrelator.h"
#include "src/flac/encoder/channel_decorrelator.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    int32_t left[8]  = {1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000};
    int32_t right[8] = {900,  -1900, 2900, -3900, 4900, -5900, 6900, -7900};

    FlacChannelAssignment modes[] = {
        FlacChannelAssignment::Independent,
        FlacChannelAssignment::LeftSide,
        FlacChannelAssignment::RightSide,
        FlacChannelAssignment::MidSide
    };

    for (auto mode : modes) {
        int32_t enc_ch0[8] = {0};
        int32_t enc_ch1[8] = {0};
        ChannelDecorrelatorEncoder::apply_decorrelation(left, right, 8, enc_ch0, enc_ch1, mode);

        ChannelDecorrelatorDecoder::undo_decorrelation(enc_ch0, enc_ch1, 8, mode);

        for (int i = 0; i < 8; ++i) {
            assert(enc_ch0[i] == left[i]);
            assert(enc_ch1[i] == right[i]);
        }
    }

    std::cout << "FLAC Decorrelator roundtrip passed!\n";
    return 0;
}
