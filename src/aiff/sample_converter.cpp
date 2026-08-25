#include "src/aiff/sample_converter.h"
#include "src/aiff/aiff_common.h"
#include "src/wav/g711.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio_codecs::aiff {

size_t bytes_per_sample(AiffSampleFormat fmt) {
    switch (fmt) {
        case AiffSampleFormat::Int8:
        case AiffSampleFormat::ALaw8:
        case AiffSampleFormat::MuLaw8:
            return 1;
        case AiffSampleFormat::Int16BE:
        case AiffSampleFormat::Int16LE:
            return 2;
        case AiffSampleFormat::Int24BE:
        case AiffSampleFormat::Int24LE:
            return 3;
        case AiffSampleFormat::Int32BE:
        case AiffSampleFormat::Int32LE:
        case AiffSampleFormat::Float32BE:
        case AiffSampleFormat::Float32LE:
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

void decode_samples_to_float(const uint8_t* in_bytes, AiffSampleFormat fmt, float* out_samples, size_t count) {
    if (!in_bytes || !out_samples || count == 0) return;

    switch (fmt) {
        case AiffSampleFormat::Int8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<float>(static_cast<int8_t>(in_bytes[i])) / 128.0f;
            }
            break;
        }
        case AiffSampleFormat::Int16BE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(read_be16(&in_bytes[i * 2]));
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
        case AiffSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(in_bytes[i * 2] | (in_bytes[i * 2 + 1] << 8));
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
        case AiffSampleFormat::Int24BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = (static_cast<int32_t>(in_bytes[i * 3]) << 24) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 1]) << 16) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 2]) << 8);
                val >>= 8;
                out_samples[i] = static_cast<float>(val) / 8388608.0f;
            }
            break;
        }
        case AiffSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = (static_cast<int32_t>(in_bytes[i * 3])) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 1]) << 8) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 2]) << 16);
                val = (val << 8) >> 8;
                out_samples[i] = static_cast<float>(val) / 8388608.0f;
            }
            break;
        }
        case AiffSampleFormat::Int32BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(read_be32(&in_bytes[i * 4]));
                out_samples[i] = static_cast<float>(val) / 2147483648.0f;
            }
            break;
        }
        case AiffSampleFormat::Int32LE: {
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
        case AiffSampleFormat::Float32BE: {
            for (size_t i = 0; i < count; ++i) {
                uint32_t u = read_be32(&in_bytes[i * 4]);
                float val;
                std::memcpy(&val, &u, sizeof(float));
                out_samples[i] = clamp_f(val, -1.0f, 1.0f);
            }
            break;
        }
        case AiffSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val;
                std::memcpy(&val, &in_bytes[i * 4], sizeof(float));
                out_samples[i] = clamp_f(val, -1.0f, 1.0f);
            }
            break;
        }
        case AiffSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = audio_codecs::wav::alaw_to_linear16(in_bytes[i]);
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
        case AiffSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = audio_codecs::wav::mulaw_to_linear16(in_bytes[i]);
                out_samples[i] = static_cast<float>(val) / 32768.0f;
            }
            break;
        }
    }
}

void decode_samples_to_i16(const uint8_t* in_bytes, AiffSampleFormat fmt, int16_t* out_samples, size_t count) {
    if (!in_bytes || !out_samples || count == 0) return;

    switch (fmt) {
        case AiffSampleFormat::Int8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int16_t>(static_cast<int8_t>(in_bytes[i])) << 8;
            }
            break;
        }
        case AiffSampleFormat::Int16BE: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int16_t>(read_be16(&in_bytes[i * 2]));
            }
            break;
        }
        case AiffSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int16_t>(in_bytes[i * 2] | (in_bytes[i * 2 + 1] << 8));
            }
            break;
        }
        case AiffSampleFormat::Int24BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = (static_cast<int32_t>(in_bytes[i * 3]) << 24) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 1]) << 16) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 2]) << 8);
                val >>= 8;
                out_samples[i] = static_cast<int16_t>(val >> 8);
            }
            break;
        }
        case AiffSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = (static_cast<int32_t>(in_bytes[i * 3])) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 1]) << 8) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 2]) << 16);
                val = (val << 8) >> 8;
                out_samples[i] = static_cast<int16_t>(val >> 8);
            }
            break;
        }
        case AiffSampleFormat::Int32BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = static_cast<int32_t>(read_be32(&in_bytes[i * 4]));
                out_samples[i] = static_cast<int16_t>(val >> 16);
            }
            break;
        }
        case AiffSampleFormat::Int32LE: {
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
        case AiffSampleFormat::Float32BE: {
            for (size_t i = 0; i < count; ++i) {
                uint32_t u = read_be32(&in_bytes[i * 4]);
                float val;
                std::memcpy(&val, &u, sizeof(float));
                out_samples[i] = clamp_i16(std::round(val * 32767.5f));
            }
            break;
        }
        case AiffSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val;
                std::memcpy(&val, &in_bytes[i * 4], sizeof(float));
                out_samples[i] = clamp_i16(std::round(val * 32767.5f));
            }
            break;
        }
        case AiffSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = audio_codecs::wav::alaw_to_linear16(in_bytes[i]);
            }
            break;
        }
        case AiffSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = audio_codecs::wav::mulaw_to_linear16(in_bytes[i]);
            }
            break;
        }
    }
}

void decode_samples_to_i32(const uint8_t* in_bytes, AiffSampleFormat fmt, int32_t* out_samples, size_t count) {
    if (!in_bytes || !out_samples || count == 0) return;

    switch (fmt) {
        case AiffSampleFormat::Int8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int32_t>(static_cast<int8_t>(in_bytes[i])) << 24;
            }
            break;
        }
        case AiffSampleFormat::Int16BE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(read_be16(&in_bytes[i * 2]));
                out_samples[i] = static_cast<int32_t>(val) << 16;
            }
            break;
        }
        case AiffSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t val = static_cast<int16_t>(in_bytes[i * 2] | (in_bytes[i * 2 + 1] << 8));
                out_samples[i] = static_cast<int32_t>(val) << 16;
            }
            break;
        }
        case AiffSampleFormat::Int24BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = (static_cast<int32_t>(in_bytes[i * 3]) << 24) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 1]) << 16) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 2]) << 8);
                out_samples[i] = val;
            }
            break;
        }
        case AiffSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t val = (static_cast<int32_t>(in_bytes[i * 3])) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 1]) << 8) |
                              (static_cast<int32_t>(in_bytes[i * 3 + 2]) << 16);
                out_samples[i] = (val << 8);
            }
            break;
        }
        case AiffSampleFormat::Int32BE: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int32_t>(read_be32(&in_bytes[i * 4]));
            }
            break;
        }
        case AiffSampleFormat::Int32LE: {
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
        case AiffSampleFormat::Float32BE: {
            for (size_t i = 0; i < count; ++i) {
                uint32_t u = read_be32(&in_bytes[i * 4]);
                float val;
                std::memcpy(&val, &u, sizeof(float));
                out_samples[i] = clamp_i32(std::round(static_cast<double>(val) * 2147483647.5), -2147483648LL, 2147483647LL);
            }
            break;
        }
        case AiffSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val;
                std::memcpy(&val, &in_bytes[i * 4], sizeof(float));
                out_samples[i] = clamp_i32(std::round(static_cast<double>(val) * 2147483647.5), -2147483648LL, 2147483647LL);
            }
            break;
        }
        case AiffSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int32_t>(audio_codecs::wav::alaw_to_linear16(in_bytes[i])) << 16;
            }
            break;
        }
        case AiffSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_samples[i] = static_cast<int32_t>(audio_codecs::wav::mulaw_to_linear16(in_bytes[i])) << 16;
            }
            break;
        }
    }
}

void encode_samples_from_float(const float* in_samples, AiffSampleFormat fmt, uint8_t* out_bytes, size_t count) {
    if (!in_samples || !out_bytes || count == 0) return;

    switch (fmt) {
        case AiffSampleFormat::Int8: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = clamp_i32(std::round(in_samples[i] * 127.5f), -128, 127);
                out_bytes[i] = static_cast<uint8_t>(static_cast<int8_t>(v));
            }
            break;
        }
        case AiffSampleFormat::Int16BE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = clamp_i16(std::round(in_samples[i] * 32767.5f));
                write_be16(&out_bytes[i * 2], static_cast<uint16_t>(v));
            }
            break;
        }
        case AiffSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = clamp_i16(std::round(in_samples[i] * 32767.5f));
                out_bytes[i * 2] = v & 0xFF;
                out_bytes[i * 2 + 1] = (v >> 8) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int24BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = clamp_i32(std::round(in_samples[i] * 8388607.5f), -8388608, 8388607);
                out_bytes[i * 3] = (v >> 16) & 0xFF;
                out_bytes[i * 3 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 3 + 2] = v & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = clamp_i32(std::round(in_samples[i] * 8388607.5f), -8388608, 8388607);
                out_bytes[i * 3] = v & 0xFF;
                out_bytes[i * 3 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 3 + 2] = (v >> 16) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int32BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = clamp_i32(std::round(static_cast<double>(in_samples[i]) * 2147483647.5), -2147483648LL, 2147483647LL);
                write_be32(&out_bytes[i * 4], static_cast<uint32_t>(v));
            }
            break;
        }
        case AiffSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = clamp_i32(std::round(static_cast<double>(in_samples[i]) * 2147483647.5), -2147483648LL, 2147483647LL);
                out_bytes[i * 4] = v & 0xFF;
                out_bytes[i * 4 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 4 + 2] = (v >> 16) & 0xFF;
                out_bytes[i * 4 + 3] = (v >> 24) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Float32BE: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                uint32_t u;
                std::memcpy(&u, &val, sizeof(float));
                write_be32(&out_bytes[i * 4], u);
            }
            break;
        }
        case AiffSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = clamp_f(in_samples[i], -1.0f, 1.0f);
                std::memcpy(&out_bytes[i * 4], &val, sizeof(float));
            }
            break;
        }
        case AiffSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = clamp_i16(std::round(in_samples[i] * 32767.5f));
                out_bytes[i] = audio_codecs::wav::linear16_to_alaw(v);
            }
            break;
        }
        case AiffSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = clamp_i16(std::round(in_samples[i] * 32767.5f));
                out_bytes[i] = audio_codecs::wav::linear16_to_mulaw(v);
            }
            break;
        }
    }
}

void encode_samples_from_i16(const int16_t* in_samples, AiffSampleFormat fmt, uint8_t* out_bytes, size_t count) {
    if (!in_samples || !out_bytes || count == 0) return;

    switch (fmt) {
        case AiffSampleFormat::Int8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = static_cast<uint8_t>(static_cast<int8_t>(in_samples[i] >> 8));
            }
            break;
        }
        case AiffSampleFormat::Int16BE: {
            for (size_t i = 0; i < count; ++i) {
                write_be16(&out_bytes[i * 2], static_cast<uint16_t>(in_samples[i]));
            }
            break;
        }
        case AiffSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i * 2] = in_samples[i] & 0xFF;
                out_bytes[i * 2 + 1] = (in_samples[i] >> 8) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int24BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = static_cast<int32_t>(in_samples[i]) << 8;
                out_bytes[i * 3] = (v >> 16) & 0xFF;
                out_bytes[i * 3 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 3 + 2] = v & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = static_cast<int32_t>(in_samples[i]) << 8;
                out_bytes[i * 3] = v & 0xFF;
                out_bytes[i * 3 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 3 + 2] = (v >> 16) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int32BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = static_cast<int32_t>(in_samples[i]) << 16;
                write_be32(&out_bytes[i * 4], static_cast<uint32_t>(v));
            }
            break;
        }
        case AiffSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = static_cast<int32_t>(in_samples[i]) << 16;
                out_bytes[i * 4] = v & 0xFF;
                out_bytes[i * 4 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 4 + 2] = (v >> 16) & 0xFF;
                out_bytes[i * 4 + 3] = (v >> 24) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Float32BE: {
            for (size_t i = 0; i < count; ++i) {
                float val = static_cast<float>(in_samples[i]) / 32768.0f;
                uint32_t u;
                std::memcpy(&u, &val, sizeof(float));
                write_be32(&out_bytes[i * 4], u);
            }
            break;
        }
        case AiffSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = static_cast<float>(in_samples[i]) / 32768.0f;
                std::memcpy(&out_bytes[i * 4], &val, sizeof(float));
            }
            break;
        }
        case AiffSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = audio_codecs::wav::linear16_to_alaw(in_samples[i]);
            }
            break;
        }
        case AiffSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = audio_codecs::wav::linear16_to_mulaw(in_samples[i]);
            }
            break;
        }
    }
}

void encode_samples_from_i32(const int32_t* in_samples, AiffSampleFormat fmt, uint8_t* out_bytes, size_t count) {
    if (!in_samples || !out_bytes || count == 0) return;

    switch (fmt) {
        case AiffSampleFormat::Int8: {
            for (size_t i = 0; i < count; ++i) {
                out_bytes[i] = static_cast<uint8_t>(static_cast<int8_t>(in_samples[i] >> 24));
            }
            break;
        }
        case AiffSampleFormat::Int16BE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = static_cast<int16_t>(in_samples[i] >> 16);
                write_be16(&out_bytes[i * 2], static_cast<uint16_t>(v));
            }
            break;
        }
        case AiffSampleFormat::Int16LE: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = static_cast<int16_t>(in_samples[i] >> 16);
                out_bytes[i * 2] = v & 0xFF;
                out_bytes[i * 2 + 1] = (v >> 8) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int24BE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = in_samples[i] >> 8;
                out_bytes[i * 3] = (v >> 16) & 0xFF;
                out_bytes[i * 3 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 3 + 2] = v & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int24LE: {
            for (size_t i = 0; i < count; ++i) {
                int32_t v = in_samples[i] >> 8;
                out_bytes[i * 3] = v & 0xFF;
                out_bytes[i * 3 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 3 + 2] = (v >> 16) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Int32BE: {
            for (size_t i = 0; i < count; ++i) {
                write_be32(&out_bytes[i * 4], static_cast<uint32_t>(in_samples[i]));
            }
            break;
        }
        case AiffSampleFormat::Int32LE: {
            for (size_t i = 0; i < count; ++i) {
                uint32_t v = static_cast<uint32_t>(in_samples[i]);
                out_bytes[i * 4] = v & 0xFF;
                out_bytes[i * 4 + 1] = (v >> 8) & 0xFF;
                out_bytes[i * 4 + 2] = (v >> 16) & 0xFF;
                out_bytes[i * 4 + 3] = (v >> 24) & 0xFF;
            }
            break;
        }
        case AiffSampleFormat::Float32BE: {
            for (size_t i = 0; i < count; ++i) {
                float val = static_cast<float>(in_samples[i]) / 2147483648.0f;
                uint32_t u;
                std::memcpy(&u, &val, sizeof(float));
                write_be32(&out_bytes[i * 4], u);
            }
            break;
        }
        case AiffSampleFormat::Float32LE: {
            for (size_t i = 0; i < count; ++i) {
                float val = static_cast<float>(in_samples[i]) / 2147483648.0f;
                std::memcpy(&out_bytes[i * 4], &val, sizeof(float));
            }
            break;
        }
        case AiffSampleFormat::ALaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = static_cast<int16_t>(in_samples[i] >> 16);
                out_bytes[i] = audio_codecs::wav::linear16_to_alaw(v);
            }
            break;
        }
        case AiffSampleFormat::MuLaw8: {
            for (size_t i = 0; i < count; ++i) {
                int16_t v = static_cast<int16_t>(in_samples[i] >> 16);
                out_bytes[i] = audio_codecs::wav::linear16_to_mulaw(v);
            }
            break;
        }
    }
}

} // namespace audio_codecs::aiff
