#include "audio_codecs/wav/wav_decoder.h"

namespace audio_codecs::wav {

template class WavDecoderBase<2>;
template class WavDecoderBase<8>;

} // namespace audio_codecs::wav
