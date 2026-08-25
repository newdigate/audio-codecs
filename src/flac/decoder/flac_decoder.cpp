#include "audio_codecs/flac/flac_decoder.h"

namespace audio_codecs::flac {

// Explicit template instantiations
template class FlacDecoderBase<1, 1024>;
template class FlacDecoderBase<2, 4096>;
template class FlacDecoderBase<8, 4096>;

} // namespace audio_codecs::flac
