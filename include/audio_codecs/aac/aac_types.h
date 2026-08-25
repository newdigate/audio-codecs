#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_codecs::aac {

enum class WindowSequence {
    OnlyLong = 0,
    LongStart = 1,
    EightShort = 2,
    LongStop = 3
};

enum class WindowShape {
    Sine = 0,
    KBD = 1
};

enum class ElementId {
    SCE = 0x0, // Single Channel Element
    CPE = 0x1, // Channel Pair Element
    CCE = 0x2, // Coupling Channel Element
    LFE = 0x3, // LFE Channel Element
    DSE = 0x4, // Data Stream Element
    PCE = 0x5, // Program Config Element
    FIL = 0x6, // Fill Element
    END = 0x7  // End Element
};

constexpr size_t AAC_FRAME_LEN_LONG = 1024;
constexpr size_t AAC_FRAME_LEN_SHORT = 128;
constexpr size_t AAC_WINDOW_LEN_LONG = 2048;
constexpr size_t AAC_WINDOW_LEN_SHORT = 256;
constexpr size_t AAC_NUM_SHORT_WINDOWS = 8;
constexpr size_t AAC_MAX_SCALEFACTOR_BANDS = 64;

} // namespace audio_codecs::aac
