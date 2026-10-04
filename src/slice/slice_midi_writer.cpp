#include <audio_codecs/slice/slice_midi_writer.h>
#include <cstring>

namespace audio_codecs::slice {

size_t SliceMidiWriter::write_variable_length_quantity(uint32_t value, uint8_t* out_buf) {
    uint8_t buffer[5];
    size_t count = 0;
    buffer[count++] = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;

    while (value > 0) {
        buffer[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
        value >>= 7;
    }

    for (size_t i = 0; i < count; ++i) {
        out_buf[i] = buffer[count - 1 - i];
    }
    return count;
}

bool SliceMidiWriter::write_type0_midi(const AslHeader& header,
                                       const AslSlice* slices,
                                       uint16_t total_slices,
                                       uint8_t* out_midi_buf,
                                       size_t max_buf_size,
                                       size_t* out_bytes_written) {
    if (slices == nullptr || total_slices == 0 || out_midi_buf == nullptr || max_buf_size < 128) {
        return false;
    }

    uint8_t* ptr = out_midi_buf;
    uint8_t* end = out_midi_buf + max_buf_size;

    // 1. MThd Chunk
    if (ptr + 14 > end) return false;
    std::memcpy(ptr, "MThd", 4); ptr += 4;
    *ptr++ = 0; *ptr++ = 0; *ptr++ = 0; *ptr++ = 6; // Header length = 6
    *ptr++ = 0; *ptr++ = 0;                         // Format 0
    *ptr++ = 0; *ptr++ = 1;                         // 1 Track
    uint16_t ppqn = (header.ppqn > 0) ? header.ppqn : ASL_PPQN;
    *ptr++ = static_cast<uint8_t>((ppqn >> 8) & 0xFF);
    *ptr++ = static_cast<uint8_t>(ppqn & 0xFF);

    // 2. MTrk Chunk Header placeholder
    if (ptr + 8 > end) return false;
    std::memcpy(ptr, "MTrk", 4); ptr += 4;
    uint8_t* track_len_ptr = ptr;
    ptr += 4; // Skip length for now
    uint8_t* track_data_start = ptr;

    // Time signature: delta 0, FF 58 04 num denom 24 08
    if (ptr + 8 > end) return false;
    *ptr++ = 0x00; // Delta time 0
    *ptr++ = 0xFF; *ptr++ = 0x58; *ptr++ = 0x04;
    *ptr++ = header.time_signature_num ? header.time_signature_num : 4;
    uint8_t denom = header.time_signature_denom ? header.time_signature_denom : 4;
    uint8_t denom_pow = 2; // 4 = 2^2
    if (denom == 1) denom_pow = 0;
    else if (denom == 2) denom_pow = 1;
    else if (denom == 4) denom_pow = 2;
    else if (denom == 8) denom_pow = 3;
    else if (denom == 16) denom_pow = 4;
    *ptr++ = denom_pow;
    *ptr++ = 24; // 24 MIDI clocks per quarter
    *ptr++ = 8;  // 8 32nd notes per quarter

    // Set Tempo: delta 0, FF 51 03 microsecs_per_quarter
    double bpm = (header.original_bpm_q16 > 0) ? (static_cast<double>(header.original_bpm_q16) / 65536.0) : 120.0;
    uint32_t us_per_quarter = static_cast<uint32_t>(60000000.0 / bpm);
    if (ptr + 7 > end) return false;
    *ptr++ = 0x00; // Delta time 0
    *ptr++ = 0xFF; *ptr++ = 0x51; *ptr++ = 0x03;
    *ptr++ = static_cast<uint8_t>((us_per_quarter >> 16) & 0xFF);
    *ptr++ = static_cast<uint8_t>((us_per_quarter >> 8) & 0xFF);
    *ptr++ = static_cast<uint8_t>(us_per_quarter & 0xFF);

    // Note events
    uint32_t current_tick = 0;
    for (uint16_t i = 0; i < total_slices; ++i) {
        const AslSlice& s = slices[i];
        uint32_t note_on_tick = s.musical_tick;
        uint32_t delta_on = (note_on_tick >= current_tick) ? (note_on_tick - current_tick) : 0;
        current_tick = note_on_tick;

        // Delta time for Note-On
        if (ptr + 8 > end) return false;
        ptr += write_variable_length_quantity(delta_on, ptr);

        // Note On: 0x90 note velocity
        *ptr++ = 0x90;
        *ptr++ = static_cast<uint8_t>(s.midi_note & 0x7F);
        *ptr++ = 100; // Default velocity

        // Note duration (approx 16th note = 120 ticks, or up to next slice)
        uint32_t note_dur = 120;
        if (i + 1 < total_slices && slices[i + 1].musical_tick > note_on_tick) {
            note_dur = slices[i + 1].musical_tick - note_on_tick;
            if (note_dur > 240) note_dur = 240; // Don't hold note excessively
        }

        // Delta time for Note-Off
        if (ptr + 8 > end) return false;
        ptr += write_variable_length_quantity(note_dur, ptr);
        current_tick += note_dur;

        // Note Off: 0x80 note 0
        *ptr++ = 0x80;
        *ptr++ = static_cast<uint8_t>(s.midi_note & 0x7F);
        *ptr++ = 0x00;
    }

    // End of Track: delta 0, FF 2F 00
    if (ptr + 4 > end) return false;
    *ptr++ = 0x00;
    *ptr++ = 0xFF; *ptr++ = 0x2F; *ptr++ = 0x00;

    // Fill Track length (Big-Endian 32-bit)
    uint32_t track_len = static_cast<uint32_t>(ptr - track_data_start);
    track_len_ptr[0] = static_cast<uint8_t>((track_len >> 24) & 0xFF);
    track_len_ptr[1] = static_cast<uint8_t>((track_len >> 16) & 0xFF);
    track_len_ptr[2] = static_cast<uint8_t>((track_len >> 8) & 0xFF);
    track_len_ptr[3] = static_cast<uint8_t>(track_len & 0xFF);

    if (out_bytes_written != nullptr) {
        *out_bytes_written = static_cast<size_t>(ptr - out_midi_buf);
    }
    return true;
}

} // namespace audio_codecs::slice
