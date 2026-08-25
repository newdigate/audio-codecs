#include "audio_codecs/aiff/aiff_encoder.h"

namespace audio_codecs::aiff {

template class AiffEncoderBase<2>;
template class AiffEncoderBase<8>;

} // namespace audio_codecs::aiff
