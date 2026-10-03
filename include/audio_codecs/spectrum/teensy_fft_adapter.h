#pragma once
#include "fft_backend.h"

namespace audio_codecs::spectrum {

// Adapter template allowing Teensy AudioAnalyzeFFT1024 or direct CMSIS-DSP
// to satisfy FftBackend without including Arduino.h in core headers.
template <typename TFft>
class TeensyAudioFftAdapter : public FftBackend {
public:
    explicit TeensyAudioFftAdapter(TFft& fft_instance) : fft_(fft_instance) {}

    void forward_1024(const int16_t* in_pcm, float* out_magnitudes_512) override {
        // TFft can be an AudioAnalyzeFFT1024-like class or CMSIS-DSP wrapper
        fft_.forward_1024(in_pcm, out_magnitudes_512);
    }

private:
    TFft& fft_;
};

} // namespace audio_codecs::spectrum
