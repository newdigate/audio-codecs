#include "src/wav/sample_converter.h"
#include "src/wav/g711.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::wav {

size_t bytes_per_sample(WavSampleFormat fmt) {
    switch (fmt) {
        case WavSampleFormat::Uint8:
        case WavSampleFormat::ALaw8:
        case WavSampleFormat::MuLaw8:
            return 1;
        case WavSampleFormat::Int16LE:
            return 2;
        case WavSampleFormat::Int24LE:
            return 3;
        case WavSampleFormat::Int32LE:
        case WavSampleFormat::Float32LE:
            return 4;
        default:
            return 2;
    }
}

static inline float clamp_f(float val, float min_val, float max_val) {
    return std::max(min_val, std::min(max_val, val));
}

static inline int32_t clamp_i32(int64_t val, int64_t min_val, int64_t max_val) {
    return static_cast<int32_t>(std::max(min_val, std::min(max_val, val)));
}

static inline int16_t clamp_i16(int32_t val) {
    return static_cast<int16_t>(std::max(-32768, std::min(32767, val)));
}

void decode_samples_to_float(const uint8_t* in_bytes, WavSampleFormat fmt, float* out_samples, size_t count) {
    if (!in_bytes || !out_samples || count == 0) return;

    switch (fmt) {
        case WavSampleFormat::Uint8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = (static_cast<float>(in_bytes[i]) - 128.0f) / 128.0f;
            }
            break;
        }
        case WavSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(in_bytes[i * 2] | (in_bytes[i * 2 + 1] << 8));
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
        case WavSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(
                    (static_cast<uint32_t>(in_bytes[i * 3])) |
                    (static_cast<uint32_t>(in_bytes[i * 3 + 1]) << 8) |
                    (static_cast<uint32_t>(in_bytes[i * 3 + 2]) << 16)
                );
                // Sign-extend 24-bit to 32-bit
                val = (val << 8) >> 8;
                out_samples[i] = static_cast<float>(val) / 8388608.0f;
            }
            break;
        }
        case WavSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(
                    (static_cast<uint32_t>(in_bytes[i * 4])) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 1]) << 8) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 2]) << 16) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 3]) << 24)
                );
                out_samples[i] = static_cast<float>(val) / 2147483648.0f;
            }
            break;
        }
        case WavSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val;
                std::memcpy(&val, &in_bytes[i * 4], sizeof(float));
                out_samples[i] = clamp_f(val, -1.0f, 1.0f);
            }
            break;
        }
        case WavSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = alaw_to_linear16(in_bytes[i]);
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
        case WavSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = mulaw_to_linear16(in_bytes[i]);
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
    }
}

void decode_samples_to_i16(const uint8_t* in_bytes, WavSampleFormat fmt, int16_t* out_samples, size_t count) {
    if (!in_bytes || !out_samples || count == 0) return;

    switch (fmt) {
        case WavSampleFormat::Uint8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int16_t>((static_cast<int32_t>(in_bytes[i]) - 128) << 8);
            }
            break;
        }
        case WavSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int16_t>(in_bytes[i * 2] | (in_bytes[i * 2 + 1] << 8));
            }
            break;
        }
        case WavSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(
                    (static_cast<uint32_t>(in_bytes[i * 3])) |
                    (static_cast<uint32_t>(in_bytes[i * 3 + 1]) << 8) |
                    (static_cast<uint32_t>(in_bytes[i * 3 + 2]) << 16)
                );
                val = (val << 8) >> 8;
                out_samples[i] = static_cast<int16_t>(val >> 8);
            }
            break;
        }
        case WavSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(
                    (static_cast<uint32_t>(in_bytes[i * 4])) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 1]) << 8) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 2]) << 16) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 3]) << 24)
                );
                out_samples[i] = static_cast<int16_t>(val >> 16);
            }
            break;
        }
        case WavSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val;
                std::memcpy(&val, &in_bytes[i * 4], sizeof(float));
                out_samples[i] = clamp_i16(static_cast<int32_t>(val >= 0.0f ? (val * 32767.0f + 0.5f) : (val * 32768.0f - 0.5f)));
            }
            break;
        }
        case WavSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = alaw_to_linear16(in_bytes[i]);
            }
            break;
        }
        case WavSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = mulaw_to_linear16(in_bytes[i]);
            }
            break;
        }
    }
}

void decode_samples_to_i32(const uint8_t* in_bytes, WavSampleFormat fmt, int32_t* out_samples, size_t count) {
    if (!in_bytes || !out_samples || count == 0) return;

    switch (fmt) {
        case WavSampleFormat::Uint8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int32_t>((static_cast<int32_t>(in_bytes[i]) - 128) << 24);
            }
            break;
        }
        case WavSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(in_bytes[i * 2] | (in_bytes[i * 2 + 1] << 8));
                out_samples[i] = static_cast<int32_t>(val) << 16;
            }
            break;
        }
        case WavSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(
                    (static_cast<uint32_t>(in_bytes[i * 3])) |
                    (static_cast<uint32_t>(in_bytes[i * 3 + 1]) << 8) |
                    (static_cast<uint32_t>(in_bytes[i * 3 + 2]) << 16)
                );
                val = (val << 8) >> 8;
                out_samples[i] = val;
            }
            break;
        }
        case WavSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int32_t>(
                    (static_cast<uint32_t>(in_bytes[i * 4])) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 1]) << 8) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 2]) << 16) |
                    (static_cast<uint32_t>(in_bytes[i * 4 + 3]) << 24)
                );
            }
            break;
        }
        case WavSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val;
                std::memcpy(&val, &in_bytes[i * 4], sizeof(float));
                out_samples[i] = clamp_i32(static_cast<int64_t>(val >= 0.0f ? (val * 2147483647.0 + 0.5) : (val * 2147483648.0 - 0.5)),
                                           -2147483647 - 1, 2147483647);
            }
            break;
        }
        case WavSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = alaw_to_linear16(in_bytes[i]);
                out_samples[i] = static_cast<int32_t>(val) << 16;
            }
            break;
        }
        case WavSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = mulaw_to_linear16(in_bytes[i]);
                out_samples[i] = static_cast<int32_t>(val) << 16;
            }
            break;
        }
    }
}

void encode_samples_from_float(const float* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count) {
    if (!in_samples || !out_bytes || count == 0) return;

    switch (fmt) {
        case WavSampleFormat::Uint8: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                int ival = static_cast<int>(val * 128.0f + 128.5f);
                if (ival < 0) ival = 0;
                if (ival > 255) ival = 255;
                out_bytes[i] = static_cast<uint8_t>(ival);
            }
            break;
        }
        case WavSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                int16_t s16 = clamp_i16(static_cast<int32_t>(val >= 0.0f ? (val * 32767.0f + 0.5f) : (val * 32768.0f - 0.5f)));
                out_bytes[i * 2]     = static_cast<uint8_t>(s16 & 0xFF);
                out_bytes[i * 2 + 1] = static_cast<uint8_t>((s16 >> 8) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                int32_t s24 = clamp_i32(static_cast<int64_t>(val >= 0.0f ? (val * 8388607.0 + 0.5) : (val * 8388608.0 - 0.5)),
                                        -8388608, 8388607);
                out_bytes[i * 3]     = static_cast<uint8_t>(s24 & 0xFF);
                out_bytes[i * 3 + 1] = static_cast<uint8_t>((s24 >> 8) & 0xFF);
                out_bytes[i * 3 + 2] = static_cast<uint8_t>((s24 >> 16) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                int32_t s32 = clamp_i32(static_cast<int64_t>(val >= 0.0f ? (val * 2147483647.0 + 0.5) : (val * 2147483648.0 - 0.5)),
                                        -2147483647 - 1, 2147483647);
                out_bytes[i * 4]     = static_cast<uint8_t>(s32 & 0xFF);
                out_bytes[i * 4 + 1] = static_cast<uint8_t>((s32 >> 8) & 0xFF);
                out_bytes[i * 4 + 2] = static_cast<uint8_t>((s32 >> 16) & 0xFF);
                out_bytes[i * 4 + 3] = static_cast<uint8_t>((s32 >> 24) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = in_samples[i];
                std::memcpy(&out_bytes[i * 4], &val, sizeof(float));
            }
            break;
        }
        case WavSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                int16_t s16 = clamp_i16(static_cast<int32_t>(val * 32767.5f));
                out_bytes[i] = linear16_to_alaw(s16);
            }
            break;
        }
        case WavSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                int16_t s16 = clamp_i16(static_cast<int32_t>(val * 32767.5f));
                out_bytes[i] = linear16_to_mulaw(s16);
            }
            break;
        }
    }
}

void encode_samples_from_i16(const int16_t* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count) {
    if (!in_samples || !out_bytes || count == 0) return;

    switch (fmt) {
        case WavSampleFormat::Uint8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = static_cast<uint8_t>((in_samples[i] >> 8) + 128);
            }
            break;
        }
        case WavSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = in_samples[i];
                out_bytes[i * 2]     = static_cast<uint8_t>(val & 0xFF);
                out_bytes[i * 2 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(in_samples[i]) << 8;
                out_bytes[i * 3]     = static_cast<uint8_t>(val & 0xFF);
                out_bytes[i * 3 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                out_bytes[i * 3 + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(in_samples[i]) << 16;
                out_bytes[i * 4]     = static_cast<uint8_t>(val & 0xFF);
                out_bytes[i * 4 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                out_bytes[i * 4 + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                out_bytes[i * 4 + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = static_cast<float>(in_samples[i]) / 32768.0f;
                std::memcpy(&out_bytes[i * 4], &val, sizeof(float));
            }
            break;
        }
        case WavSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = linear16_to_alaw(in_samples[i]);
            }
            break;
        }
        case WavSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = linear16_to_mulaw(in_samples[i]);
            }
            break;
        }
    }
}

void encode_samples_from_i32(const int32_t* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count) {
    if (!in_samples || !out_bytes || count == 0) return;

    switch (fmt) {
        case WavSampleFormat::Uint8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = static_cast<uint8_t>((in_samples[i] >> 24) + 128);
            }
            break;
        }
        case WavSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(in_samples[i] >> 16);
                out_bytes[i * 2]     = static_cast<uint8_t>(val & 0xFF);
                out_bytes[i * 2 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = in_samples[i];
                out_bytes[i * 3]     = static_cast<uint8_t>(val & 0xFF);
                out_bytes[i * 3 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                out_bytes[i * 3 + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = in_samples[i];
                out_bytes[i * 4]     = static_cast<uint8_t>(val & 0xFF);
                out_bytes[i * 4 + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                out_bytes[i * 4 + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                out_bytes[i * 4 + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
            }
            break;
        }
        case WavSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = static_cast<float>(in_samples[i]) / 2147483648.0f;
                std::memcpy(&out_bytes[i * 4], &val, sizeof(float));
            }
            break;
        }
        case WavSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = linear16_to_alaw(static_cast<int16_t>(in_samples[i] >> 16));
            }
            break;
        }
        case WavSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = linear16_to_mulaw(static_cast<int16_t>(in_samples[i] >> 16));
            }
            break;
        }
    }
}

} // namespace audio_codecs::wav
