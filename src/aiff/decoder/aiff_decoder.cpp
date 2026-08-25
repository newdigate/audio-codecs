#include "audio_codecs/aiff/aiff_decoder.h"

namespace audio_codecs::aiff {

template class AiffDecoderBase<2>;
template class AiffDecoderBase<8>;

} // namespace audio_codecs::aiff
