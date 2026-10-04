#pragma once

#include <audio_codecs/slice/slice_types.h>
#include <audio_codecs/slice/slice_detector.h>
#include <cstdint>
#include <cstddef>

namespace audio_codecs::slice {

struct GeneratorConfig {
    AslSliceMode mode = ASL_MODE_TRANSIENT_TO_GRID;
    uint32_t sample_rate = 44100;
    uint8_t channels = 2;
    uint8_t time_sig_num = 4;
    uint8_t time_sig_denom = 4;
    uint32_t bpm_q16 = (120 << 16);
    float sensitivity = 0.5f;
    uint16_t max_slices = 256;
};

class SliceGenerator {
public:
    SliceGenerator();
    bool init(const GeneratorConfig& config);
    void reset();

    bool process_pcm(const int16_t* pcm, uint32_t sample_count);
    bool finalize();

    // Sliced cooperative API
    bool step(uint32_t budget_us);
    bool is_complete() const;

    uint16_t slice_count() const;
    const AslSlice* get_slice(uint16_t index) const;
    const AslHeader& header() const;

    // Serialize to pre-allocated buffer
    bool serialize(void* out_buffer, size_t buffer_size, size_t* out_bytes_written) const;

private:
    void generate_grid_slices();
    void quantize_transients_to_grid();

    GeneratorConfig config_{};
    SliceDetector detector_;
    AslHeader header_{};
    
    static constexpr uint16_t MAX_CAPACITY = 256;
    AslSlice slices_[MAX_CAPACITY]{};
    uint16_t slice_count_ = 0;

    uint32_t raw_transients_[MAX_CAPACITY]{};
    uint16_t raw_transient_count_ = 0;
    uint32_t total_samples_ = 0;
    bool finalized_ = false;
};

} // namespace audio_codecs::slice
