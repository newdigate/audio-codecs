#pragma once

#include <cstdint>
#include <cstddef>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#error "Big-endian architectures require byte swapping for ASL1 file format"
#endif

namespace audio_codecs::slice {

constexpr uint32_t ASL_MAGIC = 0x314C5341; // 'ASL1' in Little-Endian
constexpr uint16_t ASL_VERSION = 1;
constexpr uint16_t ASL_PPQN = 480;

enum AslSliceMode : uint8_t {
    ASL_MODE_TRANSIENT         = 0,
    ASL_MODE_GRID              = 1,
    ASL_MODE_TRANSIENT_TO_GRID = 2
};

enum AslTailMode : uint8_t {
    ASL_TAIL_ONE_SHOT      = 0,
    ASL_TAIL_STRETCH_DECAY = 1,
    ASL_TAIL_SUSTAIN_LOOP  = 2
};

enum AslHeaderFlags : uint16_t {
    ASL_FLAG_NONE              = 0x0000,
    ASL_FLAG_LOOPABLE          = 0x0001,
    ASL_FLAG_HAS_SUSTAIN_LOOPS = 0x0002
};

enum AslSliceFlags : uint8_t {
    ASL_SLICE_FLAG_NONE     = 0x00,
    ASL_SLICE_FLAG_LOCKED   = 0x01,
    ASL_SLICE_FLAG_REVERSED = 0x02,
    ASL_SLICE_FLAG_MUTED    = 0x04
};

#pragma pack(push, 1)

struct AslHeader {
    uint32_t magic;                 // 0x314C5341 ('ASL1')
    uint16_t version;               // Format version: 1
    uint16_t header_size;           // Size of this header in bytes: 128
    uint32_t duration_samples;      // Total audio duration in PCM frames
    uint32_t duration_ms;           // Total audio duration in milliseconds
    uint32_t sample_rate;           // Audio sample rate (e.g. 44100, 48000)
    uint8_t  channels;              // Channel count (1 = mono, 2 = stereo)
    uint8_t  time_signature_num;    // Time signature numerator (e.g. 4)
    uint8_t  time_signature_denom;  // Time signature denominator (e.g. 4)
    uint8_t  slice_mode;            // AslSliceMode (0 = Transient, 1 = Grid, 2 = Quantized)
    uint32_t original_bpm_q16;      // Original tempo in Q16 fixed-point (e.g. 120.0 << 16)
    uint16_t ppqn;                  // Pulses Per Quarter Note (standard: 480)
    uint16_t num_bars;              // Total bars spanned by loop
    uint16_t total_slices;          // Total number of slices in file
    uint16_t slice_descriptor_size; // Size of each descriptor: 32 bytes
    uint32_t slices_offset;         // File offset to slice descriptors: 128
    uint16_t flags;                 // Bitmask of AslHeaderFlags
    uint8_t  reserved[86];          // Zero-padded future expansion (86 bytes to reach 128-byte header size)
};
static_assert(sizeof(AslHeader) == 128, "AslHeader must be exactly 128 bytes");

struct AslSlice {
    uint32_t start_sample;          // Sample offset from start of audio
    uint32_t length_samples;        // Length of slice in PCM frames
    uint32_t musical_tick;          // Musical position in PPQN ticks from start
    uint8_t  midi_note;             // Chromatic MIDI note trigger (36 = C1, 37 = C#1...)
    uint8_t  bar_index;             // Bar index (0-indexed)
    uint8_t  beat_within_bar;       // Beat index within bar (0-indexed: 0..num-1)
    uint8_t  subdivision;           // Subdivision within beat (0-indexed)
    int16_t  gain_db_q8;            // Recommended gain trim in Q8 dB (0 = 0 dB)
    uint16_t transient_energy;      // Peak transient energy novelty (0..65535)
    uint16_t decay_ms;              // Natural decay time in milliseconds
    uint8_t  tail_mode;             // AslTailMode (0 = OneShot, 1 = StretchDecay, 2 = SustainLoop)
    uint8_t  flags;                 // Bitmask of AslSliceFlags
    uint8_t  reserved[8];           // Zero-padded alignment
};
static_assert(sizeof(AslSlice) == 32, "AslSlice must be exactly 32 bytes");

#pragma pack(pop)

} // namespace audio_codecs::slice
