#include "src/mp3/mp3_tables.h"
#include <cmath>

namespace audio_codecs::mp3 {

// Bitrates in kbps: [version_idx][bitrate_idx]
// version_idx: 0 = MPEG-1, 1 = MPEG-2 / MPEG-2.5
static const uint16_t BITRATE_TABLE[2][16] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}, // MPEG-1
    {0,  8, 16, 24, 32, 40, 48, 56,  64,  80,  96, 112, 128, 144, 160, 0}  // MPEG-2 / 2.5
};

// Sample rates in Hz: [version_idx][sampling_freq_idx]
static const uint32_t SAMPLERATE_TABLE[3][4] = {
    {44100, 48000, 32000, 0}, // MPEG-1 (version_idx 0)
    {22050, 24000, 16000, 0}, // MPEG-2 (version_idx 1)
    {11025, 12000,  8000, 0}  // MPEG-2.5 (version_idx 2)
};

bool parse_frame_header(uint32_t header_word, FrameHeader& out) {
    // Check 11-bit syncword: 0xFFE00000 mask
    if ((header_word & 0xFFE00000) != 0xFFE00000) {
        return false;
    }

    uint8_t idex = static_cast<uint8_t>((header_word >> 20) & 1);
    uint8_t id   = static_cast<uint8_t>((header_word >> 19) & 1);

    if (idex == 0 && id == 0) {
        out.version = MpegVersion::Mpeg25;
    } else if (idex == 1 && id == 0) {
        out.version = MpegVersion::Mpeg2;
    } else if (idex == 1 && id == 1) {
        out.version = MpegVersion::Mpeg1;
    } else {
        return false; // Reserved
    }

    uint8_t layer_bits = static_cast<uint8_t>((header_word >> 17) & 3);
    if (layer_bits == 1) {
        out.layer = MpegLayer::Layer3;
    } else {
        return false; // Only Layer 3 supported
    }

    out.protection_bit = ((header_word >> 16) & 1) != 0;
    out.bitrate_index = static_cast<uint8_t>((header_word >> 12) & 0xF);
    if (out.bitrate_index == 0 || out.bitrate_index == 15) {
        return false; // Free format / forbidden
    }

    out.sampling_frequency = static_cast<uint8_t>((header_word >> 10) & 3);
    if (out.sampling_frequency == 3) {
        return false; // Reserved
    }

    out.padding_bit = ((header_word >> 9) & 1) != 0;
    out.private_bit = ((header_word >> 8) & 1) != 0;

    uint8_t mode_bits = static_cast<uint8_t>((header_word >> 6) & 3);
    out.mode = static_cast<MpegMode>(mode_bits);
    out.channels = (out.mode == MpegMode::SingleChannel) ? 1 : 2;

    out.mode_extension = static_cast<uint8_t>((header_word >> 4) & 3);
    if (out.mode == MpegMode::JointStereo) {
        out.intensity_stereo = (out.mode_extension & 1) != 0;
        out.ms_stereo        = (out.mode_extension & 2) != 0;
    } else {
        out.intensity_stereo = false;
        out.ms_stereo        = false;
    }

    out.copyright = ((header_word >> 3) & 1) != 0;
    out.original  = ((header_word >> 2) & 1) != 0;
    out.emphasis  = static_cast<MpegEmphasis>(header_word & 3);
    if (out.emphasis == MpegEmphasis::Reserved) {
        return false;
    }

    int v_idx = (out.version == MpegVersion::Mpeg1) ? 0 : 1;
    out.bitrate_kbps = BITRATE_TABLE[v_idx][out.bitrate_index];

    int sr_v_idx = (out.version == MpegVersion::Mpeg1) ? 0 :
                   (out.version == MpegVersion::Mpeg2) ? 1 : 2;
    out.sample_rate = SAMPLERATE_TABLE[sr_v_idx][out.sampling_frequency];

    out.ngr = (out.version == MpegVersion::Mpeg1) ? 2 : 1;

    // Frame size calculation
    if (out.version == MpegVersion::Mpeg1) {
        out.frame_bytes = (144000 * out.bitrate_kbps) / out.sample_rate + (out.padding_bit ? 1 : 0);
        out.side_info_bytes = (out.channels == 1) ? 17 : 32;
    } else {
        out.frame_bytes = (72000 * out.bitrate_kbps) / out.sample_rate + (out.padding_bit ? 1 : 0);
        out.side_info_bytes = (out.channels == 1) ? 9 : 17;
    }

    return (out.frame_bytes > 0);
}

bool build_frame_header_word(const FrameHeader& header, uint32_t& out) {
    uint32_t word = 0xFFE00000; // syncword 11 bits
    
    uint8_t idex = 1;
    uint8_t id   = 1;
    if (header.version == MpegVersion::Mpeg25) {
        idex = 0; id = 0;
    } else if (header.version == MpegVersion::Mpeg2) {
        idex = 1; id = 0;
    }
    word |= (static_cast<uint32_t>(idex) << 20);
    word |= (static_cast<uint32_t>(id)   << 19);

    // Layer 3 is 01
    word |= (1U << 17);

    // Protection bit
    if (header.protection_bit) word |= (1U << 16);

    // Bitrate index
    word |= (static_cast<uint32_t>(header.bitrate_index & 0xF) << 12);

    // Sampling frequency
    word |= (static_cast<uint32_t>(header.sampling_frequency & 3) << 10);

    // Padding bit
    if (header.padding_bit) word |= (1U << 9);

    // Private bit
    if (header.private_bit) word |= (1U << 8);

    // Mode
    word |= (static_cast<uint32_t>(header.mode) << 6);

    // Mode extension
    uint8_t mode_ext = header.mode_extension;
    if (header.mode == MpegMode::JointStereo) {
        mode_ext = (header.intensity_stereo ? 1 : 0) | (header.ms_stereo ? 2 : 0);
    }
    word |= (static_cast<uint32_t>(mode_ext & 3) << 4);

    // Copyright & Original
    if (header.copyright) word |= (1U << 3);
    if (header.original)  word |= (1U << 2);

    // Emphasis
    word |= static_cast<uint32_t>(header.emphasis) & 3;

    out = word;
    return true;
}

// Table 16: MPEG-1 scalefactor bit lengths from scalefac_compress
extern const uint8_t SLEN1_MPEG1[16] = {0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4};
extern const uint8_t SLEN2_MPEG1[16] = {0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3};

// Table 23: Preemphasis table (pretab)
extern const uint8_t PRETAB[22] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0
};

// Aliasing reduction coefficients (Table 60)
// c_i = {-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037}
// Cs[i] = 1 / sqrt(1 + c_i^2), Ca[i] = c_i / sqrt(1 + c_i^2)
extern const float ALIAS_CS[8] = {
    0.8574929257f, 0.8817419973f, 0.9496286491f, 0.9833145924f,
    0.9955178164f, 0.9991605584f, 0.9998991924f, 0.9999931550f
};

extern const float ALIAS_CA[8] = {
   -0.5144957554f, -0.4717319686f, -0.3133774542f, -0.1819132000f,
   -0.0945741926f, -0.0409655829f, -0.0141985685f, -0.0036999747f
};

// Scalefactor bands tables (monotonically increasing boundary offsets)
// Long blocks (22 bands, 23 indices)
static const uint16_t SFB_44100_LONG[23] = {
    0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576
};
static const uint16_t SFB_48000_LONG[23] = {
    0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576
};
static const uint16_t SFB_32000_LONG[23] = {
    0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576
};
static const uint16_t SFB_22050_LONG[23] = {
    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
};
static const uint16_t SFB_24000_LONG[23] = {
    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 114, 136, 162, 194, 232, 278, 332, 394, 464, 540, 576
};
static const uint16_t SFB_16000_LONG[23] = {
    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 139, 168, 200, 238, 284, 336, 396, 464, 522, 576
};
static const uint16_t SFB_11025_LONG[23] = {
    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
};
static const uint16_t SFB_12000_LONG[23] = {
    0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576
};
static const uint16_t SFB_8000_LONG[23] = {
    0, 12, 24, 36, 48, 60, 72, 88, 108, 132, 160, 192, 232, 280, 336, 400, 476, 566, 568, 570, 572, 574, 576
};

// Short blocks (13 bands, 14 indices for 192 frequency lines)
static const uint16_t SFB_44100_SHORT[14] = {
    0, 4, 8, 12, 16, 22, 30, 40, 52, 66, 84, 106, 136, 192
};
static const uint16_t SFB_48000_SHORT[14] = {
    0, 4, 8, 12, 16, 22, 28, 38, 50, 64, 80, 100, 126, 192
};
static const uint16_t SFB_32000_SHORT[14] = {
    0, 4, 8, 12, 16, 22, 30, 42, 58, 78, 104, 138, 180, 192
};
static const uint16_t SFB_22050_SHORT[14] = {
    0, 4, 8, 12, 18, 24, 32, 42, 56, 74, 100, 132, 174, 192
};
static const uint16_t SFB_24000_SHORT[14] = {
    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 136, 180, 192
};
static const uint16_t SFB_16000_SHORT[14] = {
    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192
};
static const uint16_t SFB_11025_SHORT[14] = {
    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192
};
static const uint16_t SFB_12000_SHORT[14] = {
    0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192
};
static const uint16_t SFB_8000_SHORT[14] = {
    0, 8, 16, 24, 36, 52, 72, 96, 124, 160, 162, 164, 166, 192
};

const uint16_t* get_scalefac_band_table_long(uint32_t sample_rate) {
    switch (sample_rate) {
        case 44100: return SFB_44100_LONG;
        case 48000: return SFB_48000_LONG;
        case 32000: return SFB_32000_LONG;
        case 22050: return SFB_22050_LONG;
        case 24000: return SFB_24000_LONG;
        case 16000: return SFB_16000_LONG;
        case 11025: return SFB_11025_LONG;
        case 12000: return SFB_12000_LONG;
        case 8000:  return SFB_8000_LONG;
        default:    return SFB_44100_LONG;
    }
}

const uint16_t* get_scalefac_band_table_short(uint32_t sample_rate) {
    switch (sample_rate) {
        case 44100: return SFB_44100_SHORT;
        case 48000: return SFB_48000_SHORT;
        case 32000: return SFB_32000_SHORT;
        case 22050: return SFB_22050_SHORT;
        case 24000: return SFB_24000_SHORT;
        case 16000: return SFB_16000_SHORT;
        case 11025: return SFB_11025_SHORT;
        case 12000: return SFB_12000_SHORT;
        case 8000:  return SFB_8000_SHORT;
        default:    return SFB_44100_SHORT;
    }
}

// Synthesis window D[512] coefficients
#include "src/mp3/d_window_data.inc"

// Analysis window C[512] coefficients
#include "src/mp3/c_window_data.inc"

// Huffman tables definitions
#include "src/mp3/huffman_tables_data.inc"

} // namespace audio_codecs::mp3
