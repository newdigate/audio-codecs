#include "audio_codecs/flac/flac_encoder.h"

namespace audio_codecs::flac {

// Explicit template instantiations
template class FlacEncoderBase<1, 1024>;
template class FlacEncoderBase<2, 4096>;
template class FlacEncoderBase<8, 4096>;

} // namespace audio_codecs::flac
