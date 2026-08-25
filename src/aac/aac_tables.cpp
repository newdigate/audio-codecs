#include "src/aac/aac_tables.h"
#include <cmath>
#include <vector>

namespace audio_codecs::aac {

namespace {

constexpr double PI = 3.14159265358979323846;

// Scalefactor band offsets for Long windows (1024 MDCT lines, length num_bands + 1)
// ISO/IEC 13818-7 / ISO/IEC 14496-3 Table 4.110 - 4.128

// 96 kHz (Index 0) - 41 bands
constexpr int SWB_OFFSET_96000_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 120,
    128, 136, 144, 160, 176, 196, 216, 240, 264, 304, 384, 1024
};

// 88.2 kHz (Index 1) - 41 bands
constexpr int SWB_OFFSET_88200_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 120,
    128, 136, 144, 160, 176, 196, 216, 240, 264, 304, 384, 1024
};

// 64 kHz (Index 2) - 47 bands
constexpr int SWB_OFFSET_64000_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116,
    120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208,
    216, 224, 240, 264, 304, 1024
};

// 48 kHz (Index 3) - 49 bands
constexpr int SWB_OFFSET_48000_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 116, 124,
    132, 140, 148, 160, 172, 188, 208, 232, 260, 288, 320, 356,
    396, 440, 488, 544, 612, 700, 816, 1024
};

// 44.1 kHz (Index 4) - 49 bands
constexpr int SWB_OFFSET_44100_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 116, 124,
    132, 140, 148, 160, 172, 188, 208, 232, 260, 288, 320, 356,
    396, 440, 488, 544, 612, 700, 816, 1024
};

// 32 kHz (Index 5) - 51 bands
constexpr int SWB_OFFSET_32000_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116,
    120, 124, 128, 136, 144, 152, 160, 168, 176, 184, 192, 204,
    216, 232, 252, 276, 308, 348, 400, 464, 544, 1024
};

// 24 kHz (Index 6) - 47 bands
constexpr int SWB_OFFSET_24000_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116,
    120, 128, 136, 144, 152, 164, 176, 192, 212, 236, 268, 308,
    356, 416, 492, 592, 724, 1024
};

// 22.05 kHz (Index 7) - 47 bands
constexpr int SWB_OFFSET_22050_LONG[] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
    64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116,
    120, 128, 136, 144, 152, 164, 176, 192, 212, 236, 268, 308,
    356, 416, 492, 592, 724, 1024
};

// 16 kHz (Index 8) - 43 bands
constexpr int SWB_OFFSET_16000_LONG[] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112,
    120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216,
    224, 232, 240, 248, 256, 268, 284, 308, 336, 372, 416, 472,
    544, 636, 752, 1024
};

// 12 kHz (Index 9) - 43 bands
constexpr int SWB_OFFSET_12000_LONG[] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112,
    120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216,
    224, 232, 240, 248, 268, 292, 320, 356, 400, 452, 512, 584, 668,
    768, 888, 1024
};

// 11.025 kHz (Index 10) - 43 bands
constexpr int SWB_OFFSET_11025_LONG[] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112,
    120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216,
    224, 232, 240, 248, 268, 292, 320, 356, 400, 452, 512, 584, 668,
    768, 888, 1024
};

// 8 kHz (Index 11) - 40 bands
constexpr int SWB_OFFSET_8000_LONG[] = {
    0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 168,
    180, 192, 204, 216, 228, 240, 252, 264, 276, 288, 304, 324, 348,
    376, 408, 444, 484, 528, 576, 628, 688, 756, 832, 916, 1008, 1024
};

// 7.35 kHz (Index 12) - 40 bands
constexpr int SWB_OFFSET_7350_LONG[] = {
    0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 168,
    180, 192, 204, 216, 228, 240, 252, 264, 276, 288, 304, 324, 348,
    376, 408, 444, 484, 528, 576, 628, 688, 756, 832, 916, 1008, 1024
};

// Scalefactor band offsets for Short windows (128 MDCT lines, length num_bands + 1)

// 96 kHz (Index 0) - 12 bands
constexpr int SWB_OFFSET_96000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 18, 22, 26, 32, 128
};

// 88.2 kHz (Index 1) - 12 bands
constexpr int SWB_OFFSET_88200_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 18, 22, 26, 32, 128
};

// 64 kHz (Index 2) - 12 bands
constexpr int SWB_OFFSET_64000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 128
};

// 48 kHz (Index 3) - 14 bands
constexpr int SWB_OFFSET_48000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28, 36, 44, 64, 128
};

// 44.1 kHz (Index 4) - 14 bands
constexpr int SWB_OFFSET_44100_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28, 36, 44, 64, 128
};

// 32 kHz (Index 5) - 14 bands
constexpr int SWB_OFFSET_32000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28, 36, 44, 64, 128
};

// 24 kHz (Index 6) - 15 bands
constexpr int SWB_OFFSET_24000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 18, 22, 26, 32, 40, 52, 72, 128
};

// 22.05 kHz (Index 7) - 15 bands
constexpr int SWB_OFFSET_22050_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 18, 22, 26, 32, 40, 52, 72, 128
};

// 16 kHz (Index 8) - 15 bands
constexpr int SWB_OFFSET_16000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 18, 22, 26, 32, 40, 52, 72, 128
};

// 12 kHz (Index 9) - 15 bands
constexpr int SWB_OFFSET_12000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 36, 44, 60, 128
};

// 11.025 kHz (Index 10) - 15 bands
constexpr int SWB_OFFSET_11025_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 36, 44, 60, 128
};

// 8 kHz (Index 11) - 15 bands
constexpr int SWB_OFFSET_8000_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 36, 44, 60, 128
};

// 7.35 kHz (Index 12) - 15 bands
constexpr int SWB_OFFSET_7350_SHORT[] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 36, 44, 60, 128
};

struct SwbTableEntry {
    const int* offset;
    size_t num_bands;
};

constexpr SwbTableEntry SWB_LONG_TABLES[13] = {
    { SWB_OFFSET_96000_LONG, 41 },
    { SWB_OFFSET_88200_LONG, 41 },
    { SWB_OFFSET_64000_LONG, 47 },
    { SWB_OFFSET_48000_LONG, 49 },
    { SWB_OFFSET_44100_LONG, 49 },
    { SWB_OFFSET_32000_LONG, 51 },
    { SWB_OFFSET_24000_LONG, 47 },
    { SWB_OFFSET_22050_LONG, 47 },
    { SWB_OFFSET_16000_LONG, 43 },
    { SWB_OFFSET_12000_LONG, 43 },
    { SWB_OFFSET_11025_LONG, 43 },
    { SWB_OFFSET_8000_LONG,  40 },
    { SWB_OFFSET_7350_LONG,  40 }
};

constexpr SwbTableEntry SWB_SHORT_TABLES[13] = {
    { SWB_OFFSET_96000_SHORT, 12 },
    { SWB_OFFSET_88200_SHORT, 12 },
    { SWB_OFFSET_64000_SHORT, 12 },
    { SWB_OFFSET_48000_SHORT, 14 },
    { SWB_OFFSET_44100_SHORT, 14 },
    { SWB_OFFSET_32000_SHORT, 14 },
    { SWB_OFFSET_24000_SHORT, 15 },
    { SWB_OFFSET_22050_SHORT, 15 },
    { SWB_OFFSET_16000_SHORT, 15 },
    { SWB_OFFSET_12000_SHORT, 15 },
    { SWB_OFFSET_11025_SHORT, 15 },
    { SWB_OFFSET_8000_SHORT,  15 },
    { SWB_OFFSET_7350_SHORT,  15 }
};

constexpr uint32_t SAMPLING_RATES[13] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

// Modified Bessel function of the first kind, order 0 (I0)
double bessel_i0(double x) {
    double sum = 1.0;
    double term = 1.0;
    double half_x = x * 0.5;
    for (int k = 1; k <= 50; ++k) {
        term *= (half_x / k) * (half_x / k);
        sum += term;
        if (term < 1e-15 * sum) {
            break;
        }
    }
    return sum;
}

// Generate KBD window according to ISO/IEC 13818-7 Section 8.5.1
void generate_kbd_window(float* window, int length, double alpha) {
    int half_len = length / 2;
    std::vector<double> v(half_len + 1);
    double quarter_len = length / 4.0;
    for (int n = 0; n <= half_len; ++n) {
        double temp = (n - quarter_len) / quarter_len;
        double arg = 1.0 - temp * temp;
        if (arg < 0.0) arg = 0.0;
        v[n] = bessel_i0(PI * alpha * std::sqrt(arg));
    }

    std::vector<double> sum(half_len + 1);
    sum[0] = v[0];
    for (int n = 1; n <= half_len; ++n) {
        sum[n] = sum[n - 1] + v[n];
    }

    double total_sum = sum[half_len];
    for (int n = 0; n < half_len; ++n) {
        window[n] = static_cast<float>(std::sqrt(sum[n] / total_sum));
        window[length - 1 - n] = window[n];
    }
}

// Global precomputed tables
struct AacTablesContainer {
    float sine_window_1024[2048];
    float sine_window_128[256];
    float kbd_window_1024[2048];
    float kbd_window_128[256];

    // [sequence 0..3][shape_prev 0..1][shape_curr 0..1][0..2047]
    float composite_windows[4][2][2][2048];

    float pow43_lut[257];

    AacTablesContainer() {
        // 1. Sine windows
        for (int i = 0; i < 2048; ++i) {
            sine_window_1024[i] = std::sin(static_cast<float>(PI / 2048.0 * (i + 0.5)));
        }
        for (int i = 0; i < 256; ++i) {
            sine_window_128[i] = std::sin(static_cast<float>(PI / 256.0 * (i + 0.5)));
        }

        // 2. KBD windows (alpha=4.0 for long, alpha=6.0 for short)
        generate_kbd_window(kbd_window_1024, 2048, 4.0);
        generate_kbd_window(kbd_window_128, 256, 6.0);

        // 3. Composite windows
        for (int sp = 0; sp < 2; ++sp) {
            for (int sc = 0; sc < 2; ++sc) {
                const float* prev_long = (sp == 0) ? sine_window_1024 : kbd_window_1024;
                const float* curr_long = (sc == 0) ? sine_window_1024 : kbd_window_1024;
                const float* prev_short = (sp == 0) ? sine_window_128 : kbd_window_128;
                const float* curr_short = (sc == 0) ? sine_window_128 : kbd_window_128;

                // Sequence 0: OnlyLong (length 2048)
                for (int i = 0; i < 1024; ++i) {
                    composite_windows[0][sp][sc][i] = prev_long[i];
                    composite_windows[0][sp][sc][1024 + i] = curr_long[1024 + i];
                }

                // Sequence 1: LongStart (length 2048)
                for (int i = 0; i < 1024; ++i) {
                    composite_windows[1][sp][sc][i] = prev_long[i];
                }
                for (int i = 1024; i < 1472; ++i) {
                    composite_windows[1][sp][sc][i] = 1.0f;
                }
                for (int i = 0; i < 128; ++i) {
                    composite_windows[1][sp][sc][1472 + i] = curr_short[128 + i];
                }
                for (int i = 1600; i < 2048; ++i) {
                    composite_windows[1][sp][sc][i] = 0.0f;
                }

                // Sequence 2: EightShort (length 256)
                for (int i = 0; i < 128; ++i) {
                    composite_windows[2][sp][sc][i] = prev_short[i];
                    composite_windows[2][sp][sc][128 + i] = curr_short[128 + i];
                }

                // Sequence 3: LongStop (length 2048)
                for (int i = 0; i < 448; ++i) {
                    composite_windows[3][sp][sc][i] = 0.0f;
                }
                for (int i = 0; i < 128; ++i) {
                    composite_windows[3][sp][sc][448 + i] = prev_short[i];
                }
                for (int i = 576; i < 1024; ++i) {
                    composite_windows[3][sp][sc][i] = 1.0f;
                }
                for (int i = 0; i < 1024; ++i) {
                    composite_windows[3][sp][sc][1024 + i] = curr_long[1024 + i];
                }
            }
        }

        // 4. Dequantizer pow43 lookup table
        for (int i = 0; i <= 256; ++i) {
            pow43_lut[i] = std::pow(static_cast<float>(i), 4.0f / 3.0f);
        }
    }
};

static const AacTablesContainer g_tables;

} // anonymous namespace

int get_sampling_frequency_index(uint32_t sample_rate) {
    for (int i = 0; i < 13; ++i) {
        if (SAMPLING_RATES[i] == sample_rate) {
            return i;
        }
    }
    return -1;
}

uint32_t get_sample_rate_from_index(int index) {
    if (index >= 0 && index < 13) {
        return SAMPLING_RATES[index];
    }
    return 0;
}

const int* get_swb_offset_long(uint32_t sample_rate, size_t& num_bands) {
    int idx = get_sampling_frequency_index(sample_rate);
    return get_swb_offset_long_index(idx, num_bands);
}

const int* get_swb_offset_short(uint32_t sample_rate, size_t& num_bands) {
    int idx = get_sampling_frequency_index(sample_rate);
    return get_swb_offset_short_index(idx, num_bands);
}

const int* get_swb_offset_long_index(int sf_index, size_t& num_bands) {
    if (sf_index >= 0 && sf_index < 13) {
        num_bands = SWB_LONG_TABLES[sf_index].num_bands;
        return SWB_LONG_TABLES[sf_index].offset;
    }
    num_bands = 0;
    return nullptr;
}

const int* get_swb_offset_short_index(int sf_index, size_t& num_bands) {
    if (sf_index >= 0 && sf_index < 13) {
        num_bands = SWB_SHORT_TABLES[sf_index].num_bands;
        return SWB_SHORT_TABLES[sf_index].offset;
    }
    num_bands = 0;
    return nullptr;
}

const float* get_sine_window_1024() {
    return g_tables.sine_window_1024;
}

const float* get_sine_window_128() {
    return g_tables.sine_window_128;
}

const float* get_kbd_window_1024() {
    return g_tables.kbd_window_1024;
}

const float* get_kbd_window_128() {
    return g_tables.kbd_window_128;
}

const float* get_window(WindowSequence seq, WindowShape shape_prev, WindowShape shape_curr, size_t& length) {
    int s_idx = static_cast<int>(seq);
    if (s_idx < 0 || s_idx > 3) {
        s_idx = 0;
    }
    int p_idx = (shape_prev == WindowShape::KBD) ? 1 : 0;
    int c_idx = (shape_curr == WindowShape::KBD) ? 1 : 0;

    if (seq == WindowSequence::EightShort) {
        length = 256;
    } else {
        length = 2048;
    }
    return g_tables.composite_windows[s_idx][p_idx][c_idx];
}

float dequant_pow43(int val) {
    if (val < 0) {
        val = -val;
    }
    if (val <= 256) {
        return g_tables.pow43_lut[val];
    }
    return std::pow(static_cast<float>(val), 4.0f / 3.0f);
}

} // namespace audio_codecs::aac
