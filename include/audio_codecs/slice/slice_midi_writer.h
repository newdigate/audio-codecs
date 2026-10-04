#pragma once

#include <audio_codecs/slice/slice_types.h>
#include <cstdint>
#include <cstddef>

namespace audio_codecs::slice {

class SliceMidiWriter {
public:
    static bool write_type0_midi(const AslHeader& header,
                                 const AslSlice* slices,
                                 uint16_t total_slices,
                                 uint8_t* out_midi_buf,
                                 size_t max_buf_size,
                                 size_t* out_bytes_written);

private:
    static size_t write_variable_length_quantity(uint32_t value, uint8_t* out_buf);
};

} // namespace audio_codecs::slice
