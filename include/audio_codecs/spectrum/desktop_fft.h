#pragma once
#include "fft_backend.h"

namespace audio_codecs::spectrum {

class DesktopRealFftBackend : public FftBackend {
public:
    DesktopRealFftBackend();
    ~DesktopRealFftBackend() override = default;

    bool init();
    void forward_1024(const int16_t* in_pcm, float* out_magnitudes_512) override;

private:
    float twiddle_cos_[512]{};
    float twiddle_sin_[512]{};
    uint16_t bit_reverse_[1024]{};
    bool initialized_{false};
};

} // namespace audio_codecs::spectrum
