# WAV Audio Codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a clean-room, zero-dynamic-allocation C++17 RIFF/WAVE audio encoder and decoder library supporting 8-bit, 16-bit, 24-bit, 32-bit integer PCM, 32-bit IEEE float, ITU-T G.711 A-law/$\mu$-law, and `WAVE_FORMAT_EXTENSIBLE` multi-channel audio.

**Architecture:** A streaming state machine (`WavParser`) scans RIFF/WAVE chunks with zero heap allocations using a small scratch buffer, feeding modular sample converters (`sample_converter` and `g711`) to provide normalized `float*` decoding/encoding alongside bit-exact integer overloads (`int16_t*`, `int32_t*`) in template-configurable `WavDecoderBase` and `WavEncoderBase` facades.

**Tech Stack:** C++17, CMake, standard C++ libraries (zero external dependencies).

**Spec:** `docs/superpowers/specs/2026-08-25-wav-codec-design.md`

## Global Constraints

- **Language Standard:** C++17 (`set(CMAKE_CXX_STANDARD 17)`).
- **Memory Footprint:** Zero runtime dynamic heap allocation (`no malloc / no new`) during stream decode/encode cycles.
- **Polymorphic Base Interfaces:** Conforms strictly to `audio_codecs::AudioDecoder` and `audio_codecs::AudioEncoder` base classes with normalized float `[-1.0f, +1.0f]` ranges.
- **Bit-Exact Precision:** Provide direct integer overloads for integer PCM formats ensuring bit-exact lossless roundtrips ($\text{diff} = 0$, $\text{SNR} = \infty$).
- **License:** Clean-room MIT re-write (Zero Copyleft / GPL code).

---

### Task 1: Core Type Definitions, Enums, Constants & Speaker Masks

**Files:**
- Create: `include/audio_codecs/wav/wav_types.h`
- Create: `src/wav/wav_common.h`
- Create: `include/audio_codecs/wav.h`
- Modify: `include/audio_codecs/audio_codecs.h:1-14`
- Modify: `CMakeLists.txt:1-40`
- Test: `tests/test_wav_types.cpp`

**Interfaces:**
- Consumes: `audio_codecs::AudioConfig`, `audio_codecs::AudioDecoder`, `audio_codecs::AudioEncoder` from `include/audio_codecs/core/`
- Produces: `WavFormat`, `WavSampleFormat`, `SpeakerMask`, `WavEncoderConfig`, `kGuidPcm`, `kGuidIeeeFloat` in namespace `audio_codecs::wav`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_types.cpp
#include "audio_codecs/wav/wav_types.h"
#include "src/wav/wav_common.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::wav;

    assert(static_cast<uint16_t>(WavFormat::Pcm) == 0x0001);
    assert(static_cast<uint16_t>(WavFormat::IeeeFloat) == 0x0003);
    assert(static_cast<uint16_t>(WavFormat::ALaw) == 0x0006);
    assert(static_cast<uint16_t>(WavFormat::MuLaw) == 0x0007);
    assert(static_cast<uint16_t>(WavFormat::Extensible) == 0xFFFE);

    assert(SpeakerMask::FrontLeft == 0x00000001);
    assert(SpeakerMask::FrontRight == 0x00000002);
    assert(SpeakerMask::StereoMask == 0x00000003);
    assert(SpeakerMask::Surround51Mask == 0x0000003F);

    WavEncoderConfig cfg;
    assert(cfg.sample_format == WavSampleFormat::Int16LE);
    assert(cfg.core_config.sample_rate == 44100);
    assert(cfg.core_config.channels == 2);

    assert(kFourCcRiff == 0x46464952); // "RIFF" in Little-Endian
    assert(kFourCcWave == 0x45564157); // "WAVE" in Little-Endian
    assert(kFourCcFmt  == 0x20746D66); // "fmt " in Little-Endian
    assert(kFourCcData == 0x61746164); // "data" in Little-Endian
    assert(kFourCcFact == 0x74636166); // "fact" in Little-Endian

    assert(kGuidPcm[0] == 0x01 && kGuidPcm[15] == 0x71);
    assert(kGuidIeeeFloat[0] == 0x03 && kGuidIeeeFloat[15] == 0x71);

    std::cout << "WAV types test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_wav_types`
Expected: Compilation failure due to missing `wav_types.h` and `wav_common.h`.

- [ ] **Step 3: Write minimal implementation**

Create `include/audio_codecs/wav/wav_types.h`:
```cpp
#pragma once
#include "audio_codecs/core/audio_types.h"
#include <cstddef>
#include <cstdint>

namespace audio_codecs::wav {

enum class WavFormat : uint16_t {
    Pcm        = 0x0001,
    IeeeFloat  = 0x0003,
    ALaw       = 0x0006,
    MuLaw      = 0x0007,
    Extensible = 0xFFFE
};

enum class WavSampleFormat {
    Uint8,      // 8-bit unsigned integer (0..255, 128 = silence)
    Int16LE,    // 16-bit signed integer Little-Endian
    Int24LE,    // 24-bit packed signed integer Little-Endian (3 bytes/sample)
    Int32LE,    // 32-bit signed integer Little-Endian
    Float32LE,  // 32-bit IEEE float (-1.0f .. +1.0f)
    ALaw8,      // 8-bit companded A-law
    MuLaw8      // 8-bit companded µ-law
};

enum SpeakerMask : uint32_t {
    FrontLeft          = 0x00000001,
    FrontRight         = 0x00000002,
    FrontCenter        = 0x00000004,
    LowFrequency       = 0x00000008,
    BackLeft           = 0x00000010,
    BackRight          = 0x00000020,
    FrontLeftOfCenter  = 0x00000040,
    FrontRightOfCenter = 0x00000080,
    BackCenter         = 0x00000100,
    SideLeft           = 0x00000200,
    SideRight          = 0x00000400,
    StereoMask         = FrontLeft | FrontRight,
    Surround51Mask     = FrontLeft | FrontRight | FrontCenter | LowFrequency | BackLeft | BackRight
};

struct WavEncoderConfig {
    AudioConfig core_config{44100, 2, 0, false, 0};
    WavSampleFormat sample_format{WavSampleFormat::Int16LE};
    uint32_t channel_mask{0};
    bool use_extensible{false};
};

} // namespace audio_codecs::wav
```

Create `src/wav/wav_common.h`:
```cpp
#pragma once
#include "audio_codecs/wav/wav_types.h"
#include <cstdint>

namespace audio_codecs::wav {

constexpr uint32_t make_fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
          (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
          (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
          (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kFourCcRiff = make_fourcc('R', 'I', 'F', 'F');
constexpr uint32_t kFourCcRf64 = make_fourcc('R', 'F', '6', '4');
constexpr uint32_t kFourCcWave = make_fourcc('W', 'A', 'V', 'E');
constexpr uint32_t kFourCcFmt  = make_fourcc('f', 'm', 't', ' ');
constexpr uint32_t kFourCcData = make_fourcc('d', 'a', 't', 'a');
constexpr uint32_t kFourCcFact = make_fourcc('f', 'a', 'c', 't');
constexpr uint32_t kFourCcList = make_fourcc('L', 'I', 'S', 'T');
constexpr uint32_t kFourCcBext = make_fourcc('b', 'e', 'x', 't');
constexpr uint32_t kFourCcJunk = make_fourcc('J', 'U', 'N', 'K');
constexpr uint32_t kFourCcPad  = make_fourcc('P', 'A', 'D', ' ');

constexpr uint8_t kGuidPcm[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

constexpr uint8_t kGuidIeeeFloat[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

} // namespace audio_codecs::wav
```

Create `include/audio_codecs/wav.h`:
```cpp
#pragma once
#include "audio_codecs/wav/wav_types.h"
#include "audio_codecs/wav/wav_decoder.h"
#include "audio_codecs/wav/wav_encoder.h"
```

Modify `include/audio_codecs/audio_codecs.h` to include `#include "audio_codecs/wav.h"`.
Modify `CMakeLists.txt` to add `test_wav_types`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_wav_types && ./build/test_wav_types`
Expected: `WAV types test passed!`

- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/wav/wav_types.h src/wav/wav_common.h include/audio_codecs/wav.h include/audio_codecs/audio_codecs.h tests/test_wav_types.cpp CMakeLists.txt
git commit -m "feat(wav): define WAV format enums, chunk constants and speaker masks"
```

---

### Task 2: ITU-T G.711 A-law and $\mu$-law Lookup Tables & Companding Engine

**Files:**
- Create: `src/wav/g711.h`
- Create: `src/wav/g711.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_wav_g711.cpp`

**Interfaces:**
- Consumes: Standard integer types
- Produces:
  - `int16_t alaw_to_linear16(uint8_t a_val)`
  - `uint8_t linear16_to_alaw(int16_t pcm_val)`
  - `int16_t mulaw_to_linear16(uint8_t u_val)`
  - `uint8_t linear16_to_mulaw(int16_t pcm_val)`
  - `extern const int16_t kALawToLinear16[256]`
  - `extern const int16_t kMuLawToLinear16[256]`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_g711.cpp
#include "src/wav/g711.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::wav;

    // Test A-law zero and boundaries
    uint8_t a_zero = linear16_to_alaw(0);
    int16_t a_zero_dec = alaw_to_linear16(a_zero);
    assert(std::abs(a_zero_dec) <= 8);

    uint8_t u_zero = linear16_to_mulaw(0);
    int16_t u_zero_dec = mulaw_to_linear16(u_zero);
    assert(std::abs(u_zero_dec) <= 8);

    // Test full roundtrip across all 256 code points
    for (int code = 0; code < 256; ++code) {
        uint8_t a_in = static_cast<uint8_t>(code);
        int16_t pcm_a = alaw_to_linear16(a_in);
        uint8_t a_out = linear16_to_alaw(pcm_a);
        assert(a_in == a_out);

        uint8_t u_in = static_cast<uint8_t>(code);
        int16_t pcm_u = mulaw_to_linear16(u_in);
        uint8_t u_out = linear16_to_mulaw(pcm_u);
        assert(u_in == u_out);
    }

    std::cout << "G.711 A-law and mu-law test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_wav_g711`
Expected: Build failure due to missing `g711.h` and `g711.cpp`.

- [ ] **Step 3: Write minimal implementation**

Create `src/wav/g711.h`:
```cpp
#pragma once
#include <cstdint>

namespace audio_codecs::wav {

extern const int16_t kALawToLinear16[256];
extern const int16_t kMuLawToLinear16[256];

inline int16_t alaw_to_linear16(uint8_t a_val) {
    return kALawToLinear16[a_val];
}

inline int16_t mulaw_to_linear16(uint8_t u_val) {
    return kMuLawToLinear16[u_val];
}

uint8_t linear16_to_alaw(int16_t pcm_val);
uint8_t linear16_to_mulaw(int16_t pcm_val);

} // namespace audio_codecs::wav
```

Create `src/wav/g711.cpp` implementing the 256-entry lookup tables and `linear16_to_alaw` / `linear16_to_mulaw` adhering strictly to ITU-T G.711 specs.
Modify `CMakeLists.txt` to add `src/wav/g711.cpp` into a static library `audio_codecs_wav` and add `test_wav_g711`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_wav_g711 && ./build/test_wav_g711`
Expected: `G.711 A-law and mu-law test passed!`

- [ ] **Step 5: Commit**

```bash
git add src/wav/g711.h src/wav/g711.cpp tests/test_wav_g711.cpp CMakeLists.txt
git commit -m "feat(wav): implement ITU-T G.711 A-law and mu-law table-driven companding"
```

---

### Task 3: Bit-Exact PCM & Float Sample Converters (8, 16, 24, 32-bit & IEEE Float)

**Files:**
- Create: `src/wav/sample_converter.h`
- Create: `src/wav/sample_converter.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_wav_converters.cpp`

**Interfaces:**
- Consumes: `WavSampleFormat` from `wav_types.h`, `g711.h`
- Produces:
  - `size_t bytes_per_sample(WavSampleFormat fmt)`
  - `void decode_samples_to_float(const uint8_t* in_bytes, WavSampleFormat fmt, float* out_samples, size_t count)`
  - `void decode_samples_to_i16(const uint8_t* in_bytes, WavSampleFormat fmt, int16_t* out_samples, size_t count)`
  - `void decode_samples_to_i32(const uint8_t* in_bytes, WavSampleFormat fmt, int32_t* out_samples, size_t count)`
  - `void encode_samples_from_float(const float* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count)`
  - `void encode_samples_from_i16(const int16_t* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count)`
  - `void encode_samples_from_i32(const int32_t* in_samples, WavSampleFormat fmt, uint8_t* out_bytes, size_t count)`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_converters.cpp
#include "src/wav/sample_converter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::wav;

    // 1. Test 16-bit PCM roundtrip
    int16_t s16_in[4] = {-32768, -1, 0, 32767};
    uint8_t raw16[8];
    encode_samples_from_i16(s16_in, WavSampleFormat::Int16LE, raw16, 4);
    int16_t s16_out[4];
    decode_samples_to_i16(raw16, WavSampleFormat::Int16LE, s16_out, 4);
    for (int i = 0; i < 4; ++i) assert(s16_in[i] == s16_out[i]);

    // 2. Test 24-bit PCM roundtrip
    int32_t s24_in[4] = {-8388608, -1, 0, 8388607};
    uint8_t raw24[12];
    encode_samples_from_i32(s24_in, WavSampleFormat::Int24LE, raw24, 4);
    int32_t s24_out[4];
    decode_samples_to_i32(raw24, WavSampleFormat::Int24LE, s24_out, 4);
    for (int i = 0; i < 4; ++i) assert(s24_in[i] == s24_out[i]);

    // 3. Test 8-bit unsigned roundtrip
    uint8_t u8_in[4] = {0, 128, 200, 255};
    float f_out[4];
    decode_samples_to_float(u8_in, WavSampleFormat::Uint8, f_out, 4);
    assert(std::abs(f_out[1] - 0.0f) < 1e-5f);
    assert(f_out[0] == -1.0f);
    uint8_t u8_out[4];
    encode_samples_from_float(f_out, WavSampleFormat::Uint8, u8_out, 4);
    for (int i = 0; i < 4; ++i) assert(u8_in[i] == u8_out[i]);

    // 4. Test 32-bit Float roundtrip
    float f32_in[4] = {-1.0f, -0.5f, 0.0f, 0.75f};
    uint8_t raw_f32[16];
    encode_samples_from_float(f32_in, WavSampleFormat::Float32LE, raw_f32, 4);
    float f32_out[4];
    decode_samples_to_float(raw_f32, WavSampleFormat::Float32LE, f32_out, 4);
    for (int i = 0; i < 4; ++i) assert(f32_in[i] == f32_out[i]);

    std::cout << "WAV sample converters test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_wav_converters`
Expected: Build failure due to missing `sample_converter.h`.

- [ ] **Step 3: Write minimal implementation**

Create `src/wav/sample_converter.h` and `src/wav/sample_converter.cpp` implementing the sample conversions for `Uint8`, `Int16LE`, `Int24LE`, `Int32LE`, `Float32LE`, `ALaw8`, and `MuLaw8`.
Modify `CMakeLists.txt` to add `src/wav/sample_converter.cpp` to `audio_codecs_wav` and add `test_wav_converters`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_wav_converters && ./build/test_wav_converters`
Expected: `WAV sample converters test passed!`

- [ ] **Step 5: Commit**

```bash
git add src/wav/sample_converter.h src/wav/sample_converter.cpp tests/test_wav_converters.cpp CMakeLists.txt
git commit -m "feat(wav): implement bit-exact PCM, float and G.711 sample converters"
```

---

### Task 4: Zero-Allocation Streaming RIFF/WAVE Parser (`WavParser`)

**Files:**
- Create: `src/wav/decoder/wav_parser.h`
- Create: `src/wav/decoder/wav_parser.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_wav_parser.cpp`

**Interfaces:**
- Consumes: `wav_types.h`, `wav_common.h`
- Produces:
  - `class WavParser`
  - `void WavParser::reset()`
  - `bool WavParser::parse_chunk_stream(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed)`
  - `bool WavParser::is_header_complete() const`
  - Accessors: `sample_rate()`, `channels()`, `bits_per_sample()`, `format_tag()`, `sample_format()`, `channel_mask()`, `total_samples()`, `data_chunk_size()`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_parser.cpp
#include "src/wav/decoder/wav_parser.h"
#include "src/wav/wav_common.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::wav;

    // Create standard 44-byte WAV header (16-bit stereo 44.1kHz, 1000 data bytes)
    uint8_t header[44] = {
        'R', 'I', 'F', 'F',
        36 + 1000, 0, 0, 0, // File size - 8
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,        // fmt chunk size = 16
        1, 0,               // PCM = 1
        2, 0,               // 2 channels
        0x44, 0xAC, 0, 0,   // 44100 Hz
        0x10, 0xB1, 0x02, 0,// 44100 * 4 = 176400 Bps
        4, 0,               // Block align = 4
        16, 0,              // 16 bits per sample
        'd', 'a', 't', 'a',
        0xE8, 0x03, 0, 0    // 1000 data bytes
    };

    // 1. Test parsing complete header in one call
    WavParser parser;
    size_t consumed = 0;
    bool ok = parser.parse_chunk_stream(header, sizeof(header), consumed);
    assert(ok);
    assert(parser.is_header_complete());
    assert(consumed == 44);
    assert(parser.sample_rate() == 44100);
    assert(parser.channels() == 2);
    assert(parser.bits_per_sample() == 16);
    assert(parser.format_tag() == WavFormat::Pcm);
    assert(parser.sample_format() == WavSampleFormat::Int16LE);
    assert(parser.data_chunk_size() == 1000);

    // 2. Test fragmented byte-by-byte streaming parse
    parser.reset();
    for (size_t i = 0; i < sizeof(header); ++i) {
        size_t c = 0;
        ok = parser.parse_chunk_stream(&header[i], 1, c);
        assert(ok);
        assert(c == 1);
    }
    assert(parser.is_header_complete());

    std::cout << "WAV parser test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_wav_parser`
Expected: Build failure due to missing `wav_parser.h`.

- [ ] **Step 3: Write minimal implementation**

Create `src/wav/decoder/wav_parser.h` and `src/wav/decoder/wav_parser.cpp` implementing the zero-allocation state machine scanner supporting `fmt `, `fact`, `data`, and arbitrary metadata chunk skipping (`bext`, `LIST`, `id3 `, `JUNK`, `PAD `) with odd-byte alignment support.
Modify `CMakeLists.txt` to add `src/wav/decoder/wav_parser.cpp` to `audio_codecs_wav` and add `test_wav_parser`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_wav_parser && ./build/test_wav_parser`
Expected: `WAV parser test passed!`

- [ ] **Step 5: Commit**

```bash
git add src/wav/decoder/wav_parser.h src/wav/decoder/wav_parser.cpp tests/test_wav_parser.cpp CMakeLists.txt
git commit -m "feat(wav): implement zero-allocation streaming RIFF/WAVE chunk parser"
```

---

### Task 5: Decoder Implementation (`WavDecoderBase`)

**Files:**
- Create: `include/audio_codecs/wav/wav_decoder.h`
- Create: `src/wav/decoder/wav_decoder_impl.h`
- Create: `src/wav/decoder/wav_decoder.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_wav_decoder.cpp`

**Interfaces:**
- Consumes: `AudioDecoder`, `WavParser`, `sample_converter`
- Produces: `template <size_t MaxChannels> class WavDecoderBase`, alias `using WavDecoder = WavDecoderBase<2>`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_decoder.cpp
#include "audio_codecs/wav/wav_decoder.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::wav;

    // Create 44-byte WAV header + 8 bytes of 16-bit stereo PCM (2 sample frames)
    uint8_t stream[44 + 8] = {
        'R', 'I', 'F', 'F',
        36 + 8, 0, 0, 0,
        'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',
        16, 0, 0, 0,
        1, 0,
        2, 0,
        0x80, 0xBB, 0, 0,   // 48000 Hz
        0x00, 0xEE, 0x02, 0,// 48000 * 4 = 192000 Bps
        4, 0,
        16, 0,
        'd', 'a', 't', 'a',
        8, 0, 0, 0,
        // Left 0, Right 0
        0x00, 0x00, 0x00, 0x40, // 0, 16384 (+0.5f)
        // Left 1, Right 1
        0x00, 0x80, 0x00, 0x00  // -32768 (-1.0f), 0
    };

    WavDecoder decoder;
    size_t consumed = 0;
    bool parsed = decoder.parse_stream_header(stream, sizeof(stream), consumed);
    assert(parsed);
    assert(consumed == 44);
    assert(decoder.get_sample_rate() == 48000);
    assert(decoder.get_channels() == 2);
    assert(decoder.get_bit_depth() == 16);

    float out_pcm[8] = {0};
    int decoded_samples = decoder.decode_frame(stream + 44, 8, out_pcm, 8);
    assert(decoded_samples == 4); // 4 total interleaved samples (2 frames * 2 ch)
    assert(out_pcm[0] == 0.0f);
    assert(std::abs(out_pcm[1] - 0.5f) < 1e-4f);
    assert(out_pcm[2] == -1.0f);
    assert(out_pcm[3] == 0.0f);

    std::cout << "WAV decoder test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_wav_decoder`
Expected: Build failure due to missing `wav_decoder.h`.

- [ ] **Step 3: Write minimal implementation**

Create `include/audio_codecs/wav/wav_decoder.h`, `src/wav/decoder/wav_decoder_impl.h`, and `src/wav/decoder/wav_decoder.cpp`.
Modify `CMakeLists.txt` to add `src/wav/decoder/wav_decoder.cpp` and `test_wav_decoder`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_wav_decoder && ./build/test_wav_decoder`
Expected: `WAV decoder test passed!`

- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/wav/wav_decoder.h src/wav/decoder/wav_decoder_impl.h src/wav/decoder/wav_decoder.cpp tests/test_wav_decoder.cpp CMakeLists.txt
git commit -m "feat(wav): implement WavDecoderBase with polymorphic float and integer decoders"
```

---

### Task 6: Encoder Implementation (`WavEncoderBase`)

**Files:**
- Create: `include/audio_codecs/wav/wav_encoder.h`
- Create: `src/wav/encoder/wav_encoder_impl.h`
- Create: `src/wav/encoder/wav_encoder.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_wav_encoder.cpp`

**Interfaces:**
- Consumes: `AudioEncoder`, `WavEncoderConfig`, `sample_converter`
- Produces: `template <size_t MaxChannels> class WavEncoderBase`, alias `using WavEncoder = WavEncoderBase<2>`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_encoder.cpp
#include "audio_codecs/wav/wav_encoder.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::wav;

    WavEncoder encoder;
    WavEncoderConfig config;
    config.core_config.sample_rate = 44100;
    config.core_config.channels = 2;
    config.sample_format = WavSampleFormat::Int16LE;
    bool ok = encoder.init_wav(config);
    assert(ok);

    uint8_t header_buf[128];
    int header_bytes = encoder.write_stream_header(header_buf, sizeof(header_buf));
    assert(header_bytes == 44);

    float in_pcm[4] = {0.0f, 0.5f, -1.0f, 0.0f};
    uint8_t data_buf[128];
    int encoded_bytes = encoder.encode_frame(in_pcm, 4, data_buf, sizeof(data_buf));
    assert(encoded_bytes == 8);

    // Finalize header
    encoder.finalize_header(header_buf, encoded_bytes);
    uint32_t riff_size = *reinterpret_cast<uint32_t*>(&header_buf[4]);
    uint32_t data_size = *reinterpret_cast<uint32_t*>(&header_buf[40]);
    assert(riff_size == 36 + 8);
    assert(data_size == 8);

    std::cout << "WAV encoder test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_wav_encoder`
Expected: Build failure due to missing `wav_encoder.h`.

- [ ] **Step 3: Write minimal implementation**

Create `include/audio_codecs/wav/wav_encoder.h`, `src/wav/encoder/wav_encoder_impl.h`, and `src/wav/encoder/wav_encoder.cpp` implementing standard header generation, `WAVE_FORMAT_EXTENSIBLE` headers, IEEE Float headers with `fact` chunks, sample frame encoding, and `finalize_header`.
Modify `CMakeLists.txt` to add `src/wav/encoder/wav_encoder.cpp` and `test_wav_encoder`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build --target test_wav_encoder && ./build/test_wav_encoder`
Expected: `WAV encoder test passed!`

- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/wav/wav_encoder.h src/wav/encoder/wav_encoder_impl.h src/wav/encoder/wav_encoder.cpp tests/test_wav_encoder.cpp CMakeLists.txt
git commit -m "feat(wav): implement WavEncoderBase with header generation and in-place finalizer"
```

---

### Task 7: Comprehensive Lossless & Multi-Channel Roundtrip Verification

**Files:**
- Create: `tests/test_wav_roundtrip.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_wav_roundtrip.cpp`

**Interfaces:**
- Consumes: `WavDecoderBase`, `WavEncoderBase`, `audio_codecs::wav::*`
- Produces: Full automated test suite verification

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_wav_roundtrip.cpp
#include "audio_codecs/wav/wav_decoder.h"
#include "audio_codecs/wav/wav_encoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_format_roundtrip(audio_codecs::wav::WavSampleFormat fmt, uint32_t sample_rate, uint8_t channels) {
    using namespace audio_codecs::wav;

    WavEncoderBase<8> encoder;
    WavEncoderConfig enc_cfg;
    enc_cfg.core_config.sample_rate = sample_rate;
    enc_cfg.core_config.channels = channels;
    enc_cfg.sample_format = fmt;
    assert(encoder.init_wav(enc_cfg));

    std::vector<uint8_t> stream(256);
    int hdr_len = encoder.write_stream_header(stream.data(), stream.size());
    assert(hdr_len > 0);

    const size_t num_frames = 100;
    const size_t total_samples = num_frames * channels;
    std::vector<float> orig_pcm(total_samples);
    for (size_t i = 0; i < total_samples; ++i) {
        orig_pcm[i] = std::sin(2.0f * 3.14159265f * 440.0f * (i / channels) / sample_rate) * 0.8f;
    }

    std::vector<uint8_t> payload(total_samples * 8);
    int enc_bytes = encoder.encode_frame(orig_pcm.data(), total_samples, payload.data(), payload.size());
    assert(enc_bytes > 0);

    encoder.finalize_header(stream.data(), enc_bytes);
    stream.resize(hdr_len);
    stream.insert(stream.end(), payload.begin(), payload.begin() + enc_bytes);

    // Decode
    WavDecoderBase<8> decoder;
    size_t consumed = 0;
    assert(decoder.parse_stream_header(stream.data(), stream.size(), consumed));
    assert(decoder.get_sample_rate() == sample_rate);
    assert(decoder.get_channels() == channels);

    std::vector<float> dec_pcm(total_samples);
    int dec_samples = decoder.decode_frame(stream.data() + consumed, stream.size() - consumed, dec_pcm.data(), dec_pcm.size());
    assert(dec_samples == static_cast<int>(total_samples));

    // Compute max absolute difference
    float max_diff = 0.0f;
    for (size_t i = 0; i < total_samples; ++i) {
        float diff = std::abs(orig_pcm[i] - dec_pcm[i]);
        if (diff > max_diff) max_diff = diff;
    }

    if (fmt == WavSampleFormat::Float32LE) {
        assert(max_diff < 1e-6f);
    } else if (fmt == WavSampleFormat::Int32LE || fmt == WavSampleFormat::Int24LE) {
        assert(max_diff < 1e-4f);
    } else if (fmt == WavSampleFormat::Int16LE) {
        assert(max_diff < 1e-3f);
    } else {
        assert(max_diff < 0.05f); // 8-bit / G.711 quantization error
    }
}

int main() {
    using namespace audio_codecs::wav;

    std::cout << "Testing 16-bit Stereo PCM...\n";
    test_format_roundtrip(WavSampleFormat::Int16LE, 44100, 2);

    std::cout << "Testing 24-bit 96kHz PCM...\n";
    test_format_roundtrip(WavSampleFormat::Int24LE, 96000, 2);

    std::cout << "Testing 32-bit PCM...\n";
    test_format_roundtrip(WavSampleFormat::Int32LE, 48000, 2);

    std::cout << "Testing 32-bit IEEE Float...\n";
    test_format_roundtrip(WavSampleFormat::Float32LE, 48000, 2);

    std::cout << "Testing 8-bit unsigned PCM...\n";
    test_format_roundtrip(WavSampleFormat::Uint8, 22050, 1);

    std::cout << "Testing 8-bit A-law...\n";
    test_format_roundtrip(WavSampleFormat::ALaw8, 8000, 1);

    std::cout << "Testing 8-bit mu-law...\n";
    test_format_roundtrip(WavSampleFormat::MuLaw8, 8000, 1);

    std::cout << "Testing 5.1 Surround 24-bit (Extensible)...\n";
    test_format_roundtrip(WavSampleFormat::Int24LE, 48000, 6);

    std::cout << "All WAV roundtrip tests passed successfully!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_wav_roundtrip`
Expected: Build failure before CMakeLists update.

- [ ] **Step 3: Add to CMakeLists.txt and verify all test targets**

Modify `CMakeLists.txt` to add `test_wav_roundtrip` and link to `audio_codecs_wav`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: All tests pass 100%.

- [ ] **Step 5: Commit**

```bash
git add tests/test_wav_roundtrip.cpp CMakeLists.txt
git commit -m "test(wav): add exhaustive lossless, float, G.711 and 5.1 surround roundtrip tests"
```
