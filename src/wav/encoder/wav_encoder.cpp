#include "audio_codecs/wav/wav_encoder.h"

namespace audio_codecs::wav {

template class WavEncoderBase<2>;
template class WavEncoderBase<8>;

} // namespace audio_codecs::wav
