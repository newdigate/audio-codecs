#include "src/flac/decoder/channel_decorrelator.h"

namespace audio_codecs::flac {

void ChannelDecorrelatorDecoder::undo_decorrelation(int32_t* ch0, int32_t* ch1, size_t count, FlacChannelAssignment mode) {
    if (!ch0 || !ch1 || count == 0) return;

    switch (mode) {
        case FlacChannelAssignment::Independent:
            // Nothing to do
            break;

        case FlacChannelAssignment::LeftSide:
            // ch0 = Left, ch1 = Side (Left - Right)
            // Right = Left - Side
            for (size_t i = 0; i < count; ++i) {
                ch1[i] = ch0[i] - ch1[i];
            }
            break;

        case FlacChannelAssignment::RightSide:
            // ch0 = Side (Left - Right), ch1 = Right
            // Left = Right + Side
            for (size_t i = 0; i < count; ++i) {
                ch0[i] = ch1[i] + ch0[i];
            }
            break;

        case FlacChannelAssignment::MidSide:
            // ch0 = Mid (floor((L + R)/2)), ch1 = Side (L - R)
            // L = Mid + floor((Side + (Side % 2))/2)
            // R = L - Side
            for (size_t i = 0; i < count; ++i) {
                int32_t mid = ch0[i];
                int32_t side = ch1[i];
                int32_t left = mid + ((side + (side & 1)) / 2);
                int32_t right = left - side;
                ch0[i] = left;
                ch1[i] = right;
            }
            break;
    }
}

} // namespace audio_codecs::flac
