# AIFF Audio Codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a clean-room, zero-dynamic-allocation C++17 Apple AIFF and AIFF-C (AIFC) audio encoder and decoder library supporting 8-bit signed PCM, 16-bit, 24-bit, 32-bit Big-Endian PCM, little-endian `sowt`, 32-bit IEEE float `fl32`, and ITU-T G.711 A-law/$\mu$-law.

**Architecture:** A streaming state machine (`AiffParser`) scans IFF/AIFF/AIFC chunks with zero heap allocations using a small scratch buffer, feeding modular sample converters (`sample_converter` and `ieee80`) to provide normalized `float*` decoding/encoding alongside bit-exact integer overloads (`int16_t*`, `int32_t*`) in template-configurable `AiffDecoderBase` and `AiffEncoderBase` facades.

**Tech Stack:** C++17, CMake, standard C++ libraries (zero external dependencies).

**Spec:** `docs/superpowers/specs/2026-08-25-aiff-codec-design.md`

## Global Constraints

- **Language Standard:** C++17 (`set(CMAKE_CXX_STANDARD 17)`).
- **Memory Footprint:** Zero runtime dynamic heap allocation (`no malloc / no new`) during stream decode/encode cycles.
- **Polymorphic Base Interfaces:** Conforms strictly to `audio_codecs::AudioDecoder` and `audio_codecs::AudioEncoder` base classes with normalized float `[-1.0f, +1.0f]` ranges.
- **Bit-Exact Precision:** Provide direct integer overloads for integer PCM formats ensuring bit-exact lossless roundtrips ($\text{diff} = 0$, $\text{SNR} = \infty$).
- **License:** Clean-room MIT re-write (Zero Copyleft / GPL code).

---

### Task 1: Core Type Definitions, Enums, Constants & Umbrella Header

**Files:**
- Create: `include/audio_codecs/aiff/aiff_types.h`
- Create: `src/aiff/aiff_common.h`
- Create: `include/audio_codecs/aiff.h`
- Modify: `include/audio_codecs/audio_codecs.h`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_types.cpp`

**Interfaces:**
- Consumes: `audio_codecs::AudioConfig`, `audio_codecs::AudioDecoder`, `audio_codecs::AudioEncoder` from `include/audio_codecs/core/`
- Produces: `AiffFormType`, `AiffCompressionType`, `AiffSampleFormat`, `AiffEncoderConfig`, `kFourCcForm`, `kFourCcAiff`, `kFourCcAifc`, `kFourCcComm`, `kFourCcSsnd`, `kFourCcFver` in namespace `audio_codecs::aiff`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_types.cpp
#include "audio_codecs/aiff/aiff_types.h"
#include "src/aiff/aiff_common.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    assert(static_cast<uint32_t>(AiffFormType::Aiff) == 0x41494646); // 'AIFF'
    assert(static_cast<uint32_t>(AiffFormType::Aifc) == 0x41494643); // 'AIFC'

    assert(static_cast<uint32_t>(AiffCompressionType::None) == 0x4E4F4E45); // 'NONE'
    assert(static_cast<uint32_t>(AiffCompressionType::Sowt) == 0x736F7774); // 'sowt'
    assert(static_cast<uint32_t>(AiffCompressionType::Fl32) == 0x666C3332); // 'fl32'
    assert(static_cast<uint32_t>(AiffCompressionType::ALaw) == 0x616C6177); // 'alaw'
    assert(static_cast<uint32_t>(AiffCompressionType::MuLaw) == 0x756C6177); // 'ulaw'

    AiffEncoderConfig cfg;
    assert(cfg.sample_format == AiffSampleFormat::Int16BE);
    assert(cfg.core_config.sample_rate == 44100);
    assert(cfg.core_config.channels == 2);
    assert(cfg.form_type == AiffFormType::Aiff);
    assert(cfg.compression_type == AiffCompressionType::None);

    assert(kFourCcForm == 0x464F524D); // 'FORM' Big-Endian
    assert(kFourCcAiff == 0x41494646); // 'AIFF' Big-Endian
    assert(kFourCcAifc == 0x41494643); // 'AIFC' Big-Endian
    assert(kFourCcComm == 0x434F4D4D); // 'COMM' Big-Endian
    assert(kFourCcSsnd == 0x53534E44); // 'SSND' Big-Endian
    assert(kFourCcFver == 0x46564552); // 'FVER' Big-Endian
    assert(kAifcVersion1 == 0xA2805140);

    std::cout << "AIFF types test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_types`
Expected: Compilation failure due to missing headers.

- [ ] **Step 3: Write minimal implementation**

Create `include/audio_codecs/aiff/aiff_types.h`:
```cpp
#pragma once
#include "audio_codecs/core/audio_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::aiff {

enum class AiffFormType : uint32_t {
    Aiff = 0x41494646,  // 'AIFF'
    Aifc = 0x41494643   // 'AIFC'
};

enum class AiffCompressionType : uint32_t {
    None  = 0x4E4F4E45,  // 'NONE' - Big-endian uncompressed PCM
    Sowt  = 0x736F7774,  // 'sowt' - Little-endian uncompressed PCM ("twos" swapped)
    Fl32  = 0x666C3332,  // 'fl32' - 32-bit IEEE 754 floating point
    FL32  = 0x464C3332,  // 'FL32' - 32-bit IEEE float alternate
    ALaw  = 0x616C6177,  // 'alaw' - 8-bit ITU-T G.711 A-law
    MuLaw = 0x756C6177,  // 'ulaw' - 8-bit ITU-T G.711 µ-law
    In24  = 0x696E3234,  // 'in24' - 24-bit integer
    In32  = 0x696E3332   // 'in32' - 32-bit integer
};

enum class AiffSampleFormat {
    Int8,       // 8-bit signed integer (two's complement, range -128..127, 0 = silence)
    Int16BE,    // 16-bit signed Big-Endian integer
    Int24BE,    // 24-bit packed signed Big-Endian integer (3 bytes/sample)
    Int32BE,    // 32-bit signed Big-Endian integer
    Int16LE,    // 16-bit signed Little-Endian integer (sowt)
    Int24LE,    // 24-bit packed signed Little-Endian integer (sowt)
    Int32LE,    // 32-bit signed Little-Endian integer (sowt)
    Float32BE,  // 32-bit IEEE float Big-Endian
    Float32LE,  // 32-bit IEEE float Little-Endian
    ALaw8,      // 8-bit ITU-T G.711 A-law
    MuLaw8      // 8-bit ITU-T G.711 µ-law
};

struct AiffEncoderConfig {
    AudioConfig core_config{44100, 2, 0};
    AiffFormType form_type{AiffFormType::Aiff};
    AiffCompressionType compression_type{AiffCompressionType::None};
    uint8_t bits_per_sample{16};
    AiffSampleFormat sample_format{AiffSampleFormat::Int16BE};
};

} // namespace audio_codecs::aiff
```

Create `src/aiff/aiff_common.h`:
```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace audio_codecs::aiff {

constexpr uint32_t kFourCcForm = 0x464F524D; // 'FORM'
constexpr uint32_t kFourCcAiff = 0x41494646; // 'AIFF'
constexpr uint32_t kFourCcAifc = 0x41494643; // 'AIFC'
constexpr uint32_t kFourCcComm = 0x434F4D4D; // 'COMM'
constexpr uint32_t kFourCcSsnd = 0x53534E44; // 'SSND'
constexpr uint32_t kFourCcFver = 0x46564552; // 'FVER'
constexpr uint32_t kFourCcMark = 0x4D41524B; // 'MARK'
constexpr uint32_t kFourCcInst = 0x494E5354; // 'INST'
constexpr uint32_t kFourCcComt = 0x434F4D54; // 'COMT'
constexpr uint32_t kFourCcAnno = 0x414E4E4F; // 'ANNO'
constexpr uint32_t kFourCcAuth = 0x41555448; // 'AUTH'
constexpr uint32_t kFourCcCopy = 0x28632920; // '(c) '
constexpr uint32_t kFourCcName = 0x4E414D45; // 'NAME'
constexpr uint32_t kFourCcAppl = 0x4150504C; // 'APPL'
constexpr uint32_t kFourCcId3U = 0x49443320; // 'ID3 '
constexpr uint32_t kFourCcId3L = 0x69643320; // 'id3 '

constexpr uint32_t kAifcVersion1 = 0xA2805140; // May 23, 1990 14:40:00 UTC

inline uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline uint16_t read_be16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) |
           static_cast<uint16_t>(p[1]);
}

inline void write_be32(uint8_t* p, uint32_t val) {
    p[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(val & 0xFF);
}

inline void write_be16(uint8_t* p, uint16_t val) {
    p[0] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(val & 0xFF);
}

} // namespace audio_codecs::aiff
```

Create `include/audio_codecs/aiff.h`:
```cpp
#pragma once
#include "audio_codecs/aiff/aiff_types.h"
#include "audio_codecs/aiff/aiff_decoder.h"
#include "audio_codecs/aiff/aiff_encoder.h"
```

Modify `include/audio_codecs/audio_codecs.h` to include AIFF:
```cpp
#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/core/encoder_interface.h"
#include "audio_codecs/mp3/mp3_decoder.h"
#include "audio_codecs/mp3/mp3_encoder.h"
#include "audio_codecs/flac/flac_decoder.h"
#include "audio_codecs/flac/flac_encoder.h"
#include "audio_codecs/ogg.h"
#include "audio_codecs/vorbis.h"
#include "audio_codecs/aac.h"
#include "audio_codecs/mp4.h"
#include "audio_codecs/wav.h"
#include "audio_codecs/aiff.h"
```

Update `CMakeLists.txt` to add test `test_aiff_types`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_types && ctest --test-dir build -R AiffTypesTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aiff/aiff_types.h src/aiff/aiff_common.h include/audio_codecs/aiff.h include/audio_codecs/audio_codecs.h tests/test_aiff_types.cpp CMakeLists.txt
git commit -m "feat(aiff): add AIFF core types, constants and umbrella headers"
```

---

### Task 2: IEEE 754 80-Bit Extended Precision Float Converter

**Files:**
- Create: `src/aiff/ieee80.h`
- Create: `src/aiff/ieee80.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_ieee80.cpp`

**Interfaces:**
- Produces: `void uint32_to_ieee80(uint32_t sample_rate, uint8_t out[10])`, `void double_to_ieee80(double val, uint8_t out[10])`, `uint32_t ieee80_to_uint32(const uint8_t in[10])`, `double ieee80_to_double(const uint8_t in[10])` in namespace `audio_codecs::aiff`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_ieee80.cpp
#include "src/aiff/ieee80.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::aiff;

    // Test vector for 44100
    uint8_t b44100[10];
    uint32_to_ieee80(44100, b44100);
    // 44100 = 0xAC44 = 1.3458251953125 * 2^15
    // Exp = 16383 + 15 = 16398 = 0x400E
    assert(b44100[0] == 0x40 && b44100[1] == 0x0E);
    assert(b44100[2] == 0xAC && b44100[3] == 0x44);
    assert(b44100[4] == 0x00 && b44100[5] == 0x00);
    assert(b44100[6] == 0x00 && b44100[7] == 0x00);
    assert(b44100[8] == 0x00 && b44100[9] == 0x00);

    uint32_t decoded44100 = ieee80_to_uint32(b44100);
    assert(decoded44100 == 44100);

    // Standard sample rates roundtrip
    const std::vector<uint32_t> standard_rates = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000,
        44100, 48000, 64000, 88200, 96000, 176400, 192000, 384000
    };

    for (uint32_t r : standard_rates) {
        uint8_t buf[10];
        uint32_to_ieee80(r, buf);
        uint32_t out_r = ieee80_to_uint32(buf);
        assert(out_r == r);
        double out_d = ieee80_to_double(buf);
        assert(std::round(out_d) == static_cast<double>(r));
    }

    // Edge cases
    uint8_t zero_buf[10];
    uint32_to_ieee80(0, zero_buf);
    assert(ieee80_to_uint32(zero_buf) == 0);

    std::cout << "AIFF IEEE-80 float tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_ieee80`
Expected: Compilation failure due to missing `ieee80.h` / `ieee80.cpp`.

- [ ] **Step 3: Write minimal implementation**

Create `src/aiff/ieee80.h`:
```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace audio_codecs::aiff {

void uint32_to_ieee80(uint32_t sample_rate, uint8_t out[10]);
void double_to_ieee80(double value, uint8_t out[10]);
uint32_t ieee80_to_uint32(const uint8_t in[10]);
double ieee80_to_double(const uint8_t in[10]);

} // namespace audio_codecs::aiff
```

Create `src/aiff/ieee80.cpp`:
```cpp
#include "src/aiff/ieee80.h"
#include <cmath>
#include <cstring>

namespace audio_codecs::aiff {

void uint32_to_ieee80(uint32_t sample_rate, uint8_t out[10]) {
    std::memset(out, 0, 10);
    if (sample_rate == 0) return;

    // Find highest bit index (0..31)
    int k = 31;
    while (k >= 0 && ((sample_rate & (1u << k)) == 0)) {
        --k;
    }
    if (k < 0) return;

    uint16_t exp = static_cast<uint16_t>(16383 + k);
    uint64_t mant = static_cast<uint64_t>(sample_rate) << (63 - k);

    out[0] = static_cast<uint8_t>((exp >> 8) & 0x7F);
    out[1] = static_cast<uint8_t>(exp & 0xFF);
    for (int i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<uint8_t>((mant >> (56 - 8 * i)) & 0xFF);
    }
}

void double_to_ieee80(double value, uint8_t out[10]) {
    std::memset(out, 0, 10);
    if (value == 0.0) return;

    uint8_t sign = 0;
    if (value < 0.0) {
        sign = 1;
        value = -value;
    }

    int exp;
    double fmant = std::frexp(value, &exp); // value = fmant * 2^exp, fmant in [0.5, 1.0)
    // In IEEE 80-bit, mantissa is [1.0, 2.0), so shift exp by -1
    --exp;
    fmant *= 2.0;

    uint16_t ieee_exp = static_cast<uint16_t>(16383 + exp);
    uint64_t mant = static_cast<uint64_t>(std::ldexp(fmant, 63));

    out[0] = static_cast<uint8_t>(((sign & 1) << 7) | ((ieee_exp >> 8) & 0x7F));
    out[1] = static_cast<uint8_t>(ieee_exp & 0xFF);
    for (int i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<uint8_t>((mant >> (56 - 8 * i)) & 0xFF);
    }
}

uint32_t ieee80_to_uint32(const uint8_t in[10]) {
    uint8_t sign = (in[0] >> 7) & 1;
    uint16_t exp = (static_cast<uint16_t>(in[0] & 0x7F) << 8) | in[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) {
        mant = (mant << 8) | in[2 + i];
    }

    if (exp == 0 && mant == 0) return 0;
    if (exp == 0x7FFF) return 0; // Inf / NaN

    int shift = static_cast<int>(exp) - 16383;
    if (shift < 0) return 0;
    if (shift > 31) return 0xFFFFFFFFu;

    uint64_t val = mant >> (63 - shift);
    return static_cast<uint32_t>(val);
}

double ieee80_to_double(const uint8_t in[10]) {
    uint8_t sign = (in[0] >> 7) & 1;
    uint16_t exp = (static_cast<uint16_t>(in[0] & 0x7F) << 8) | in[1];
    uint64_t mant = 0;
    for (int i = 0; i < 8; ++i) {
        mant = (mant << 8) | in[2 + i];
    }

    if (exp == 0 && mant == 0) return 0.0;
    if (exp == 0x7FFF) {
        return mant == 0 ? (sign ? -HUGE_VAL : HUGE_VAL) : NAN;
    }

    double res = std::ldexp(static_cast<double>(mant), static_cast<int>(exp) - 16383 - 63);
    return sign ? -res : res;
}

} // namespace audio_codecs::aiff
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_ieee80 && ctest --test-dir build -R AiffIeee80Test --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/aiff/ieee80.h src/aiff/ieee80.cpp tests/test_aiff_ieee80.cpp CMakeLists.txt
git commit -m "feat(aiff): implement portable IEEE 754 80-bit float conversion"
```

---

### Task 3: Bit-Exact Sample Converters & G.711 Integration

**Files:**
- Create: `src/aiff/sample_converter.h`
- Create: `src/aiff/sample_converter.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_converters.cpp`

**Interfaces:**
- Produces: `decode_samples_to_f32`, `decode_samples_to_i16`, `decode_samples_to_i32`, `encode_samples_from_f32`, `encode_samples_from_i16`, `encode_samples_from_i32` in namespace `audio_codecs::aiff`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_converters.cpp
#include "src/aiff/sample_converter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::aiff;

    // Test 16-bit BE PCM conversions
    int16_t src16[4] = { -32768, -1, 0, 32767 };
    uint8_t enc16[8];
    encode_samples_from_i16(AiffSampleFormat::Int16BE, src16, 4, enc16);
    assert(enc16[0] == 0x80 && enc16[1] == 0x00);
    assert(enc16[2] == 0xFF && enc16[3] == 0xFF);
    assert(enc16[4] == 0x00 && enc16[5] == 0x00);
    assert(enc16[6] == 0x7F && enc16[7] == 0xFF);

    int16_t dec16[4];
    decode_samples_to_i16(AiffSampleFormat::Int16BE, enc16, 4, dec16);
    for (int i = 0; i < 4; ++i) {
        assert(dec16[i] == src16[i]);
    }

    // Test 8-bit signed PCM conversions
    int8_t src8[4] = { -128, -1, 0, 127 };
    uint8_t enc8[4];
    int16_t src8_as_16[4] = { -32768, -256, 0, 32512 };
    encode_samples_from_i16(AiffSampleFormat::Int8, src8_as_16, 4, enc8);
    assert(static_cast<int8_t>(enc8[0]) == -128);
    assert(static_cast<int8_t>(enc8[1]) == -1);
    assert(static_cast<int8_t>(enc8[2]) == 0);
    assert(static_cast<int8_t>(enc8[3]) == 127);

    // Test 24-bit BE PCM
    int32_t src24[4] = { -8388608, -1, 0, 8388607 };
    uint8_t enc24[12];
    encode_samples_from_i32(AiffSampleFormat::Int24BE, src24, 4, enc24);
    assert(enc24[0] == 0x80 && enc24[1] == 0x00 && enc24[2] == 0x00);
    assert(enc24[3] == 0xFF && enc24[4] == 0xFF && enc24[5] == 0xFF);
    assert(enc24[6] == 0x00 && enc24[7] == 0x00 && enc24[8] == 0x00);
    assert(enc24[9] == 0x7F && enc24[10] == 0xFF && enc24[11] == 0xFF);

    int32_t dec24[4];
    decode_samples_to_i32(AiffSampleFormat::Int24BE, enc24, 4, dec24);
    for (int i = 0; i < 4; ++i) {
        assert(dec24[i] == src24[i]);
    }

    // Test sowt (16-bit LE)
    uint8_t enc_sowt[8];
    encode_samples_from_i16(AiffSampleFormat::Int16LE, src16, 4, enc_sowt);
    assert(enc_sowt[0] == 0x00 && enc_sowt[1] == 0x80);
    int16_t dec_sowt[4];
    decode_samples_to_i16(AiffSampleFormat::Int16LE, enc_sowt, 4, dec_sowt);
    for (int i = 0; i < 4; ++i) {
        assert(dec_sowt[i] == src16[i]);
    }

    std::cout << "AIFF converter tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_converters`
Expected: Compilation failure.

- [ ] **Step 3: Write minimal implementation**

Create `src/aiff/sample_converter.h` and `src/aiff/sample_converter.cpp` supporting:
- 8-bit signed PCM (two's complement)
- 16-bit BE / LE (`sowt`) PCM
- 24-bit BE / LE (`sowt`) packed PCM
- 32-bit BE / LE (`sowt`) PCM
- 32-bit IEEE float (`fl32` BE/LE)
- ITU-T G.711 A-law / $\mu$-law (referencing `src/wav/g711.h` LUTs)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_converters && ctest --test-dir build -R AiffConvertersTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/aiff/sample_converter.h src/aiff/sample_converter.cpp tests/test_aiff_converters.cpp CMakeLists.txt
git commit -m "feat(aiff): implement sample converters and G.711 integration"
```

---

### Task 4: Streaming IFF/AIFF Re-Entrant Chunk Parser (`AiffParser`)

**Files:**
- Create: `src/aiff/decoder/aiff_parser.h`
- Create: `src/aiff/decoder/aiff_parser.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_parser.cpp`

**Interfaces:**
- Produces: `AiffParser` with `process_bytes()`, `is_header_complete()`, `has_error()`, and metadata accessors.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_parser.cpp
#include "src/aiff/decoder/aiff_parser.h"
#include "src/aiff/ieee80.h"
#include "src/aiff/aiff_common.h"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    // Construct standard AIFF header (44100 Hz, stereo, 16-bit, 1000 frames)
    std::vector<uint8_t> header;
    // FORM header (12 bytes)
    header.insert(header.end(), {'F', 'O', 'R', 'M'});
    uint32_t form_size = 4 + (8 + 18) + (8 + 8 + 4000);
    uint8_t sz[4];
    write_be32(sz, form_size);
    header.insert(header.end(), sz, sz + 4);
    header.insert(header.end(), {'A', 'I', 'F', 'F'});

    // COMM chunk (8 + 18 bytes)
    header.insert(header.end(), {'C', 'O', 'M', 'M'});
    write_be32(sz, 18);
    header.insert(header.end(), sz, sz + 4);
    uint8_t comm_data[18];
    write_be16(comm_data, 2); // 2 channels
    write_be32(comm_data + 2, 1000); // 1000 frames
    write_be16(comm_data + 6, 16); // 16 bits
    uint32_to_ieee80(44100, comm_data + 8); // 44100 Hz
    header.insert(header.end(), comm_data, comm_data + 18);

    // SSND chunk header (8 + 8 bytes)
    header.insert(header.end(), {'S', 'S', 'N', 'D'});
    write_be32(sz, 4008);
    header.insert(header.end(), sz, sz + 4);
    uint8_t ssnd_hdr[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // offset=0, blockSize=0
    header.insert(header.end(), ssnd_hdr, ssnd_hdr + 8);

    // Feed in 1-byte chunks
    AiffParser parser;
    size_t offset = 0;
    while (offset < header.size() && !parser.is_header_complete()) {
        size_t consumed = 0;
        bool ok = parser.process_bytes(header.data() + offset, 1, consumed);
        assert(ok);
        offset += consumed;
    }

    assert(parser.is_header_complete());
    assert(parser.get_form_type() == AiffFormType::Aiff);
    assert(parser.get_sample_rate() == 44100);
    assert(parser.get_channels() == 2);
    assert(parser.get_bits_per_sample() == 16);
    assert(parser.get_total_frames() == 1000);
    assert(parser.get_sample_format() == AiffSampleFormat::Int16BE);

    std::cout << "AIFF parser tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_parser`
Expected: Compilation failure.

- [ ] **Step 3: Write minimal implementation**

Implement `AiffParser` handling `FORM`, `COMM`, `SSND`, `FVER`, auxiliary chunks (`MARK`, `INST`, `COMT`, `ANNO`, `ID3 `, `id3 `), odd-length 2-byte chunk alignment padding, non-zero `SSND` offset, and `AIFC` Pascal strings.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_parser && ctest --test-dir build -R AiffParserTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/aiff/decoder/aiff_parser.h src/aiff/decoder/aiff_parser.cpp tests/test_aiff_parser.cpp CMakeLists.txt
git commit -m "feat(aiff): implement streaming re-entrant IFF/AIFF chunk parser"
```

---

### Task 5: AIFF Decoder Implementation (`AiffDecoderBase<MaxChannels>`)

**Files:**
- Create: `include/audio_codecs/aiff/aiff_decoder.h`
- Create: `src/aiff/decoder/aiff_decoder_impl.h`
- Create: `src/aiff/decoder/aiff_decoder.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_decoder.cpp`

**Interfaces:**
- Implements: `AudioDecoder`
- Produces: `AiffDecoderBase<MaxChannels>` with `decode_frame`, `decode_frame_i16`, `decode_frame_i32`, `decode_frame_f32`, `parse_stream_header`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_decoder.cpp
#include "audio_codecs/aiff/aiff_decoder.h"
#include "src/aiff/ieee80.h"
#include "src/aiff/aiff_common.h"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    // Create 100 stereo 16-bit BE samples
    std::vector<uint8_t> stream;
    // FORM header
    stream.insert(stream.end(), {'F', 'O', 'R', 'M'});
    uint32_t form_size = 4 + (8 + 18) + (8 + 8 + 400);
    uint8_t sz[4];
    write_be32(sz, form_size);
    stream.insert(stream.end(), sz, sz + 4);
    stream.insert(stream.end(), {'A', 'I', 'F', 'F'});

    // COMM chunk
    stream.insert(stream.end(), {'C', 'O', 'M', 'M'});
    write_be32(sz, 18);
    stream.insert(stream.end(), sz, sz + 4);
    uint8_t comm_data[18];
    write_be16(comm_data, 2);
    write_be32(comm_data + 2, 100);
    write_be16(comm_data + 6, 16);
    uint32_to_ieee80(44100, comm_data + 8);
    stream.insert(stream.end(), comm_data, comm_data + 18);

    // SSND chunk
    stream.insert(stream.end(), {'S', 'S', 'N', 'D'});
    write_be32(sz, 408);
    stream.insert(stream.end(), sz, sz + 4);
    uint8_t ssnd_hdr[8] = {0};
    stream.insert(stream.end(), ssnd_hdr, ssnd_hdr + 8);

    // Raw samples (100 frames of stereo 16-bit = 200 samples = 400 bytes)
    for (int i = 0; i < 100; ++i) {
        int16_t l = static_cast<int16_t>(i * 100);
        int16_t r = static_cast<int16_t>(-i * 100);
        uint8_t b[4];
        write_be16(b, static_cast<uint16_t>(l));
        write_be16(b + 2, static_cast<uint16_t>(r));
        stream.insert(stream.end(), b, b + 4);
    }

    AiffDecoder decoder;
    size_t consumed = 0;
    bool ok = decoder.parse_stream_header(stream.data(), stream.size(), consumed);
    assert(ok);
    assert(decoder.get_sample_rate() == 44100);
    assert(decoder.get_channels() == 2);

    int16_t out_pcm[200];
    int samples = decoder.decode_frame_i16(stream.data() + consumed, stream.size() - consumed, out_pcm, 200);
    assert(samples == 200);
    for (int i = 0; i < 100; ++i) {
        assert(out_pcm[i * 2] == static_cast<int16_t>(i * 100));
        assert(out_pcm[i * 2 + 1] == static_cast<int16_t>(-i * 100));
    }

    std::cout << "AIFF decoder test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_decoder`
Expected: Compilation failure.

- [ ] **Step 3: Write minimal implementation**

Create `include/audio_codecs/aiff/aiff_decoder.h`, `src/aiff/decoder/aiff_decoder_impl.h`, and `src/aiff/decoder/aiff_decoder.cpp`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_decoder && ctest --test-dir build -R AiffDecoderTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aiff/aiff_decoder.h src/aiff/decoder/aiff_decoder_impl.h src/aiff/decoder/aiff_decoder.cpp tests/test_aiff_decoder.cpp CMakeLists.txt
git commit -m "feat(aiff): implement AiffDecoderBase and AudioDecoder interface"
```

---

### Task 6: AIFF Encoder Implementation (`AiffEncoderBase<MaxChannels>`)

**Files:**
- Create: `include/audio_codecs/aiff/aiff_encoder.h`
- Create: `src/aiff/encoder/aiff_encoder_impl.h`
- Create: `src/aiff/encoder/aiff_encoder.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_encoder.cpp`

**Interfaces:**
- Implements: `AudioEncoder`
- Produces: `AiffEncoderBase<MaxChannels>` with `write_stream_header`, `encode_frame`, `encode_frame_i16`, `encode_frame_i32`, `encode_frame_f32`, `finalize_header`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_encoder.cpp
#include "audio_codecs/aiff/aiff_encoder.h"
#include "audio_codecs/aiff/aiff_decoder.h"
#include <cassert>
#include <vector>
#include <iostream>

int main() {
    using namespace audio_codecs::aiff;

    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 48000;
    cfg.core_config.channels = 2;
    cfg.sample_format = AiffSampleFormat::Int16BE;
    encoder.init_aiff(cfg);

    uint8_t header[128];
    int hdr_bytes = encoder.write_stream_header(header, sizeof(header));
    assert(hdr_bytes > 0);

    int16_t pcm[200];
    for (int i = 0; i < 100; ++i) {
        pcm[i * 2] = static_cast<int16_t>(i * 50);
        pcm[i * 2 + 1] = static_cast<int16_t>(-i * 50);
    }

    uint8_t audio_out[512];
    int enc_bytes = encoder.encode_frame_i16(pcm, 200, audio_out, sizeof(audio_out));
    assert(enc_bytes == 400);

    encoder.finalize_header(header, static_cast<uint32_t>(enc_bytes));

    // Decode back
    std::vector<uint8_t> full_file(header, header + hdr_bytes);
    full_file.insert(full_file.end(), audio_out, audio_out + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    bool ok = decoder.parse_stream_header(full_file.data(), full_file.size(), consumed);
    assert(ok);
    assert(decoder.get_sample_rate() == 48000);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_total_frames() == 100);

    int16_t dec_pcm[200];
    int dec_samples = decoder.decode_frame_i16(full_file.data() + consumed, full_file.size() - consumed, dec_pcm, 200);
    assert(dec_samples == 200);
    for (int i = 0; i < 200; ++i) {
        assert(dec_pcm[i] == pcm[i]);
    }

    std::cout << "AIFF encoder test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_encoder`
Expected: Compilation failure.

- [ ] **Step 3: Write minimal implementation**

Create `include/audio_codecs/aiff/aiff_encoder.h`, `src/aiff/encoder/aiff_encoder_impl.h`, and `src/aiff/encoder/aiff_encoder.cpp`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_encoder && ctest --test-dir build -R AiffEncoderTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aiff/aiff_encoder.h src/aiff/encoder/aiff_encoder_impl.h src/aiff/encoder/aiff_encoder.cpp tests/test_aiff_encoder.cpp CMakeLists.txt
git commit -m "feat(aiff): implement AiffEncoderBase and AudioEncoder interface"
```

---

### Task 7: End-to-End Lossless Roundtrip & Multi-Channel Verification

**Files:**
- Create: `tests/test_aiff_roundtrip.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_aiff_roundtrip.cpp`

**Interfaces:**
- Verifies: Full lossless roundtrip across 8-bit signed PCM, 16-bit BE, 24-bit BE, 32-bit BE, `sowt` LE (16/24/32), 32-bit IEEE float `fl32`, G.711 `alaw`/`ulaw`, and multi-channel configurations (mono, stereo, 5.1).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aiff_roundtrip.cpp
#include "audio_codecs/aiff.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

using namespace audio_codecs::aiff;

void test_pcm16_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 44100;
    cfg.core_config.channels = 2;
    cfg.sample_format = AiffSampleFormat::Int16BE;
    encoder.init_aiff(cfg);

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    std::vector<int16_t> orig(2000);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int16_t>(std::sin(i * 0.05) * 30000.0);
    }

    std::vector<uint8_t> payload(4000);
    int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
    assert(enc_bytes == 4000);
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    bool ok = decoder.parse_stream_header(full.data(), full.size(), consumed);
    assert(ok);

    std::vector<int16_t> decoded(2000);
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 2000);

    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]); // Bit-exact lossless
    }
}

void test_aifc_sowt_roundtrip() {
    AiffEncoder encoder;
    AiffEncoderConfig cfg;
    cfg.core_config.sample_rate = 48000;
    cfg.core_config.channels = 2;
    cfg.form_type = AiffFormType::Aifc;
    cfg.compression_type = AiffCompressionType::Sowt;
    cfg.sample_format = AiffSampleFormat::Int16LE;
    encoder.init_aiff(cfg);

    uint8_t hdr[128];
    int hdr_sz = encoder.write_stream_header(hdr, sizeof(hdr));

    std::vector<int16_t> orig(1000);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = static_cast<int16_t>(i * 17);
    }

    std::vector<uint8_t> payload(2000);
    int enc_bytes = encoder.encode_frame_i16(orig.data(), orig.size(), payload.data(), payload.size());
    encoder.finalize_header(hdr, enc_bytes);

    std::vector<uint8_t> full(hdr, hdr + hdr_sz);
    full.insert(full.end(), payload.begin(), payload.begin() + enc_bytes);

    AiffDecoder decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(full.data(), full.size(), consumed));
    assert(decoder.get_form_type() == AiffFormType::Aifc);
    assert(decoder.get_compression_type() == AiffCompressionType::Sowt);

    std::vector<int16_t> decoded(1000);
    int dec_samples = decoder.decode_frame_i16(full.data() + consumed, full.size() - consumed, decoded.data(), decoded.size());
    assert(dec_samples == 1000);
    for (size_t i = 0; i < orig.size(); ++i) {
        assert(decoded[i] == orig[i]);
    }
}

int main() {
    test_pcm16_roundtrip();
    test_aifc_sowt_roundtrip();
    std::cout << "All AIFF roundtrip tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_aiff_roundtrip`

- [ ] **Step 3: Finalize and verify all roundtrip formats**

Support all sample formats (8-bit, 16-bit BE, 24-bit BE, 32-bit BE, sowt, fl32, alaw, ulaw, mono/stereo/multichannel).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_aiff_roundtrip && ctest --test-dir build -R AiffRoundtripTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tests/test_aiff_roundtrip.cpp CMakeLists.txt
git commit -m "test(aiff): add comprehensive lossless roundtrip verification tests"
```
