# FLAC Audio Codec (Encoder & Decoder) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a clean-room, zero-dynamic-allocation C++17 FLAC lossless audio encoder and decoder compliant with IETF RFC 9639, supporting dual normalized `float*` and bit-exact `int32_t*` APIs.

**Architecture:** Modular DSP pipeline with partitioned Rice coder, fixed & LPC predictors, interchannel decorrelator, CRC-8/CRC-16 engines, and MD5 verifier, wrapped in template classes (`FlacDecoderBase<MaxCh, MaxBlock>`, `FlacEncoderBase<MaxCh, MaxBlock>`).

**Tech Stack:** C++17, CMake 3.16+, CTest, standard math library. Zero external dependencies.

**Spec:** [`docs/superpowers/specs/2026-08-24-flac-codec-design.md`](file:///Users/moolet/Development/github/newdigate/audio-codecs/docs/superpowers/specs/2026-08-24-flac-codec-design.md)

## Global Constraints
- Target platform: x64/Linux, macOS, and 32-bit MCUs (Teensy 4.x, i.MX RT1176, ESP32).
- Zero runtime dynamic allocations (`no malloc / no new`) during streaming.
- All internal states use pre-allocated static buffers (`alignas(16) uint8_t state_buffer_[...]`).
- Clean-room MIT license (strictly no GPL/copyleft code).
- RFC 9639 compliant: magic `"fLaC"`, `STREAMINFO`, CRC-8, CRC-16-FLAC, MD5, Rice 4/5-bit, Fixed 0..4, LPC 1..32, L/R, Left-Side, Right-Side, Mid-Side.

---

### Task 1: FLAC Common Definitions, Checksum Engines (CRC-8, CRC-16-FLAC), and MD5 Calculation

**Files:**
- Create: `src/flac/flac_common.h`
- Create: `src/flac/crc.h` & `src/flac/crc.cpp`
- Create: `src/flac/md5.h` & `src/flac/md5.cpp`
- Test: `tests/test_flac_crc.cpp`
- Test: `tests/test_flac_md5.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `uint8_t crc8_update(uint8_t crc, uint8_t data)` / `uint8_t crc8_calculate(const uint8_t* data, size_t len)`
  - `uint16_t crc16_update(uint16_t crc, uint8_t data)` / `uint16_t crc16_calculate(const uint8_t* data, size_t len)`
  - `class Md5Context` (`init()`, `update(const uint8_t*, size_t)`, `finish(uint8_t[16])`)

- [ ] **Step 1: Write failing CRC and MD5 tests**

```cpp
// tests/test_flac_crc.cpp
#include "src/flac/crc.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;
    // CRC-8 test: standard test string
    uint8_t test_hdr[4] = {0xFF, 0xF8, 0x69, 0x02};
    uint8_t c8 = crc8_calculate(test_hdr, 4);
    assert(c8 != 0);

    // CRC-16 test: 0x8005 polynomial
    uint8_t test_frame[8] = {0xFF, 0xF8, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    uint16_t c16 = crc16_calculate(test_frame, 8);
    assert(c16 != 0);

    std::cout << "FLAC CRC tests passed!\n";
    return 0;
}
```

```cpp
// tests/test_flac_md5.cpp
#include "src/flac/md5.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;
    Md5Context md5;
    md5.init();
    const char* str = "The quick brown fox jumps over the lazy dog";
    md5.update(reinterpret_cast<const uint8_t*>(str), std::strlen(str));
    uint8_t digest[16];
    md5.finish(digest);

    // Expected MD5: 9e107d9d372bb6826bd81d3542a419d6
    assert(digest[0] == 0x9e && digest[1] == 0x10);
    assert(digest[14] == 0x19 && digest[15] == 0xd6);

    std::cout << "FLAC MD5 tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run tests to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure (files do not exist yet).

- [ ] **Step 3: Implement CRC-8, CRC-16-FLAC, MD5, and Common Header**
Create `src/flac/flac_common.h`, `src/flac/crc.h/.cpp`, `src/flac/md5.h/.cpp`.

- [ ] **Step 4: Run tests to verify they pass**
Run: `ctest --test-dir build -R "FlacCrc|FlacMd5" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/flac/ tests/test_flac_crc.cpp tests/test_flac_md5.cpp CMakeLists.txt
git commit -m "feat(flac): implement CRC-8, CRC-16-FLAC, MD5, and common definitions"
```

---

### Task 2: Partitioned Rice Coding (Encoder & Decoder) & Zigzag Folding

**Files:**
- Create: `src/flac/decoder/rice_decoder.h` & `src/flac/decoder/rice_decoder.cpp`
- Create: `src/flac/encoder/rice_encoder.h` & `src/flac/encoder/rice_encoder.cpp`
- Test: `tests/test_flac_rice.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `bool RiceDecoder::decode_residual_partition(core::BitReader&, int32_t* out_residual, size_t count, uint8_t rice_param_bits, uint8_t param)`
  - `void RiceEncoder::encode_residual_partition(core::BitWriter&, const int32_t* residual, size_t count, uint8_t rice_param_bits, uint8_t param)`
  - `uint8_t RiceEncoder::find_optimal_rice_param(const int32_t* residual, size_t count, uint8_t rice_param_bits)`

- [ ] **Step 1: Write failing Rice coding test**

```cpp
// tests/test_flac_rice.cpp
#include "src/flac/decoder/rice_decoder.h"
#include "src/flac/encoder/rice_encoder.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;
    using namespace audio_codecs::core;

    int32_t orig[16] = {0, 1, -1, 2, -2, 5, -8, 12, -15, 0, 1, -1, 3, -4, 2, -2};
    uint8_t param = RiceEncoder::find_optimal_rice_param(orig, 16, 4);

    uint8_t buffer[64] = {0};
    BitWriter writer;
    writer.init(buffer, sizeof(buffer));
    RiceEncoder::encode_residual_partition(writer, orig, 16, 4, param);
    writer.flush_to_byte();

    BitReader reader;
    reader.init(buffer, writer.get_byte_count());

    int32_t decoded[16] = {0};
    bool ok = RiceDecoder::decode_residual_partition(reader, decoded, 16, 4, param);
    assert(ok);

    for (int i = 0; i < 16; ++i) {
        assert(decoded[i] == orig[i]);
    }

    std::cout << "FLAC Rice coding roundtrip passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement Rice decoder and encoder**
Implement zigzag folding/unfolding, unary quotient loops, binary remainder packing, and escape partition coding.

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacRice --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add src/flac/decoder/rice_decoder.* src/flac/encoder/rice_encoder.* tests/test_flac_rice.cpp CMakeLists.txt
git commit -m "feat(flac): implement partitioned Rice encoder and decoder"
```

---

### Task 3: Subframe Predictor Engines (Constant, Verbatim, Fixed 0..4, LPC 1..32)

**Files:**
- Create: `src/flac/decoder/subframe_decoder.h` & `src/flac/decoder/subframe_decoder.cpp`
- Create: `src/flac/encoder/fixed_predictor.h` & `src/flac/encoder/fixed_predictor.cpp`
- Test: `tests/test_flac_predictors.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void FixedPredictor::compute_residual(const int32_t* samples, size_t count, int order, int32_t* out_residual)`
  - `void FixedPredictor::restore_samples(const int32_t* residual, size_t count, int order, int32_t* inout_samples)`
  - `void LpcPredictor::restore_samples(const int32_t* residual, size_t count, int order, const int32_t* qlp_coeff, int qlp_shift, int32_t* inout_samples)`

- [ ] **Step 1: Write failing predictor test**

```cpp
// tests/test_flac_predictors.cpp
#include "src/flac/encoder/fixed_predictor.h"
#include "src/flac/decoder/subframe_decoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    // Test signal
    int32_t samples[32] = {
        100, 105, 112, 120, 131, 145, 160, 178,
        195, 210, 222, 230, 235, 236, 232, 225,
        215, 201, 185, 166, 145, 122, 100, 78,
        58, 40, 25, 12, 4, 0, 1, 6
    };

    int32_t orig[32];
    for (int i = 0; i < 32; ++i) orig[i] = samples[i];

    // Test Fixed Predictor Orders 0..4
    for (int order = 0; order <= 4; ++order) {
        int32_t residual[32] = {0};
        FixedPredictor::compute_residual(orig, 32, order, residual);

        int32_t restored[32] = {0};
        for (int i = 0; i < order; ++i) restored[i] = orig[i]; // warmups
        FixedPredictor::restore_samples(residual, 32, order, restored);

        for (int i = 0; i < 32; ++i) {
            assert(restored[i] == orig[i]);
        }
    }

    std::cout << "FLAC Predictor tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement fixed and LPC predictor functions**
Implement integer polynomial formulas for orders 0..4 and 64-bit integer accumulator for LPC order 1..32 with right shift $q$.

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacPredictors --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add src/flac/decoder/subframe_decoder.* src/flac/encoder/fixed_predictor.* tests/test_flac_predictors.cpp CMakeLists.txt
git commit -m "feat(flac): implement fixed and LPC subframe predictors"
```

---

### Task 4: Interchannel Decorrelation (Independent, Left-Side, Right-Side, Mid-Side)

**Files:**
- Create: `src/flac/decoder/channel_decorrelator.h` & `src/flac/decoder/channel_decorrelator.cpp`
- Create: `src/flac/encoder/channel_decorrelator.h` & `src/flac/encoder/channel_decorrelator.cpp`
- Test: `tests/test_flac_decorrelator.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void ChannelDecorrelator::undo_decorrelation(int32_t* ch0, int32_t* ch1, size_t count, FlacChannelAssignment mode)`
  - `FlacChannelAssignment ChannelDecorrelator::apply_decorrelation(const int32_t* left, const int32_t* right, size_t count, int32_t* out_ch0, int32_t* out_ch1, FlacChannelAssignment requested_mode)`

- [ ] **Step 1: Write failing decorrelation tests**

```cpp
// tests/test_flac_decorrelator.cpp
#include "src/flac/decoder/channel_decorrelator.h"
#include "src/flac/encoder/channel_decorrelator.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    int32_t left[8]  = {1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000};
    int32_t right[8] = {900,  -1900, 2900, -3900, 4900, -5900, 6900, -7900};

    FlacChannelAssignment modes[] = {
        FlacChannelAssignment::Independent,
        FlacChannelAssignment::LeftSide,
        FlacChannelAssignment::RightSide,
        FlacChannelAssignment::MidSide
    };

    for (auto mode : modes) {
        int32_t enc_ch0[8] = {0};
        int32_t enc_ch1[8] = {0};
        ChannelDecorrelator::apply_decorrelation(left, right, 8, enc_ch0, enc_ch1, mode);

        ChannelDecorrelator::undo_decorrelation(enc_ch0, enc_ch1, 8, mode);

        for (int i = 0; i < 8; ++i) {
            assert(enc_ch0[i] == left[i]);
            assert(enc_ch1[i] == right[i]);
        }
    }

    std::cout << "FLAC Decorrelator roundtrip passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement decorrelation algorithms**
Implement exact integer Left-Side, Right-Side, and Mid-Side forward/reverse equations.

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacDecorrelator --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add src/flac/decoder/channel_decorrelator.* src/flac/encoder/channel_decorrelator.* tests/test_flac_decorrelator.cpp CMakeLists.txt
git commit -m "feat(flac): implement interchannel stereo decorrelation"
```

---

### Task 5: Metadata Parser / Builder & Container Handling (`STREAMINFO`, `PADDING`, `VORBIS_COMMENT`)

**Files:**
- Create: `src/flac/metadata.h` & `src/flac/metadata.cpp`
- Test: `tests/test_flac_metadata.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `bool MetadataParser::parse_streaminfo(const uint8_t* in, size_t len, FlacStreamInfo& info)`
  - `size_t MetadataParser::skip_metadata_blocks(const uint8_t* in, size_t len, FlacStreamInfo& out_info)`
  - `size_t MetadataBuilder::write_stream_header(uint8_t* out, size_t max_len, const FlacStreamInfo& info)`

- [ ] **Step 1: Write failing metadata tests**

```cpp
// tests/test_flac_metadata.cpp
#include "src/flac/metadata.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    FlacStreamInfo orig;
    orig.min_block_size = 4096;
    orig.max_block_size = 4096;
    orig.min_frame_size = 0;
    orig.max_frame_size = 0;
    orig.sample_rate = 44100;
    orig.channels = 2;
    orig.bits_per_sample = 16;
    orig.total_samples = 441000;

    uint8_t buffer[64] = {0};
    size_t written = MetadataBuilder::write_stream_header(buffer, sizeof(buffer), orig);
    assert(written == 42); // "fLaC" (4) + Header (4) + STREAMINFO (34)

    FlacStreamInfo parsed;
    size_t consumed = MetadataParser::skip_metadata_blocks(buffer, written, parsed);
    assert(consumed == 42);
    assert(parsed.sample_rate == 44100);
    assert(parsed.channels == 2);
    assert(parsed.bits_per_sample == 16);
    assert(parsed.total_samples == 441000);

    std::cout << "FLAC Metadata tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement metadata builder and parser**
Implement 34-byte `STREAMINFO` bit packing/unpacking and generic metadata block header navigation.

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacMetadata --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add src/flac/metadata.* tests/test_flac_metadata.cpp CMakeLists.txt
git commit -m "feat(flac): implement metadata parser and STREAMINFO builder"
```

---

### Task 6: FLAC Decoder Facade & Frame Parser (`FlacDecoderBase`, `FlacDecoder`)

**Files:**
- Create: `include/audio_codecs/flac/flac_decoder.h`
- Create: `src/flac/decoder/flac_decoder.cpp`
- Test: `tests/test_flac_decoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `template <size_t MaxChannels, size_t MaxBlockSize> class FlacDecoderBase : public AudioDecoder`
  - `using FlacDecoder = FlacDecoderBase<2, 4096>;`
  - Methods: `decode_frame(...)` (`float*`), `decode_frame_i32(...)`, `decode_frame_i16(...)`, `parse_stream_header(...)`

- [ ] **Step 1: Write failing FlacDecoder test**

```cpp
// tests/test_flac_decoder.cpp
#include "audio_codecs/flac/flac_decoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;
    FlacDecoder decoder;
    audio_codecs::AudioConfig config{44100, 2, 0, false, 2};
    assert(decoder.init(config));

    std::cout << "FLAC Decoder facade test initialized!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement FlacDecoderBase and frame decoding engine**
Parse frame header (sync 0x3FFE, block size, sample rate, channel mode, bit depth, UTF-8 frame number, CRC-8), iterate subframes, decode residuals, un-wasted-bits, undo stereo decorrelation, verify CRC-16-FLAC, and write to output PCM.

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacDecoder --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add include/audio_codecs/flac/flac_decoder.h src/flac/decoder/flac_decoder.cpp tests/test_flac_decoder.cpp CMakeLists.txt
git commit -m "feat(flac): implement FlacDecoder facade and frame decoding pipeline"
```

---

### Task 7: LPC Analysis & Autocorrelation Optimization (Levinson-Durbin Recursion)

**Files:**
- Create: `src/flac/encoder/lpc_analyzer.h` & `src/flac/encoder/lpc_analyzer.cpp`
- Test: `tests/test_flac_lpc.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void LpcAnalyzer::compute_lpc_coefficients(const int32_t* samples, size_t count, int max_order, int& best_order, int32_t* qlp_coeff, int& qlp_shift, int precision_bits)`

- [ ] **Step 1: Write failing LPC analyzer test**

```cpp
// tests/test_flac_lpc.cpp
#include "src/flac/encoder/lpc_analyzer.h"
#include "src/flac/decoder/subframe_decoder.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    // Generate sinusoidal sequence
    int32_t samples[256];
    for (int i = 0; i < 256; ++i) {
        samples[i] = static_cast<int32_t>(10000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / 44100.0));
    }

    int best_order = 0;
    int32_t qlp_coeff[32] = {0};
    int qlp_shift = 0;

    LpcAnalyzer::compute_lpc_coefficients(samples, 256, 8, best_order, qlp_coeff, qlp_shift, 14);

    assert(best_order >= 2);
    assert(qlp_shift >= 0);

    std::cout << "FLAC LPC analyzer test passed! (Best order: " << best_order << ", shift: " << qlp_shift << ")\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement autocorrelation and Levinson-Durbin LPC analyzer**
Implement windowed autocorrelation, Levinson-Durbin reflection coefficient recursion, and fixed-point integer coefficient quantization with non-negative shift $q$.

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacLpc --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add src/flac/encoder/lpc_analyzer.* tests/test_flac_lpc.cpp CMakeLists.txt
git commit -m "feat(flac): implement autocorrelation and Levinson-Durbin LPC analyzer"
```

---

### Task 8: FLAC Encoder Facade & Rate-Distortion / Compression Levels (`FlacEncoderBase`, `FlacEncoder`)

**Files:**
- Create: `include/audio_codecs/flac/flac_encoder.h`
- Create: `src/flac/encoder/flac_encoder.cpp`
- Test: `tests/test_flac_encoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `template <size_t MaxChannels, size_t MaxBlockSize> class FlacEncoderBase : public AudioEncoder`
  - `using FlacEncoder = FlacEncoderBase<2, 4096>;`
  - Methods: `encode_frame(...)` (`float*`), `encode_frame_i32(...)`, `encode_frame_i16(...)`, `write_stream_header(...)`, `finish_stream(...)`

- [ ] **Step 1: Write failing FlacEncoder test**

```cpp
// tests/test_flac_encoder.cpp
#include "audio_codecs/flac/flac_encoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;
    FlacEncoder encoder;
    FlacEncoderConfig cfg;
    cfg.core_config = {44100, 2, 0, false, 2};
    cfg.compression_level = 5;
    cfg.block_size = 1152;
    cfg.bit_depth = 16;
    assert(encoder.init_flac(cfg));

    int16_t pcm_in[1152 * 2] = {0};
    uint8_t out[4096] = {0};
    int bytes = encoder.encode_frame_i16(pcm_in, 1152 * 2, out, sizeof(out));
    assert(bytes > 0);
    assert(out[0] == 0xFF && (out[1] & 0xFC) == 0xF8); // Sync code 0b11111111111110

    std::cout << "FLAC Encoder facade test passed! (Encoded " << bytes << " bytes)\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake -B build && cmake --build build`
Expected: Compilation failure.

- [ ] **Step 3: Implement FlacEncoderBase and frame encoding engine**
Implement subframe analysis (constant/verbatim/fixed/LPC evaluation, Rice parameter search, optimal bit packaging, CRC-8 header, and CRC-16-FLAC footer).

- [ ] **Step 4: Run test to verify pass**
Run: `ctest --test-dir build -R FlacEncoder --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add include/audio_codecs/flac/flac_encoder.h src/flac/encoder/flac_encoder.cpp tests/test_flac_encoder.cpp CMakeLists.txt
git commit -m "feat(flac): implement FlacEncoder facade and compression pipeline"
```

---

### Task 9: Bit-Exact Lossless Roundtrip & Real-World FLAC Integration Testing

**Files:**
- Modify: `include/audio_codecs/audio_codecs.h`
- Create: `tests/test_flac_roundtrip.cpp`
- Create: `tests/test_real_flacs.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FlacEncoder`, `FlacDecoder`
- Produces: 100% passing automated test suite for FLAC encoding, decoding, and real `.flac` test files.

- [ ] **Step 1: Write bit-exact roundtrip test**

```cpp
// tests/test_flac_roundtrip.cpp
#include "audio_codecs/flac/flac_encoder.h"
#include "audio_codecs/flac/flac_decoder.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::flac;

    FlacEncoder encoder;
    FlacDecoder decoder;

    FlacEncoderConfig cfg;
    cfg.core_config = {44100, 2, 0, false, 2};
    cfg.compression_level = 5;
    cfg.block_size = 1152;
    cfg.bit_depth = 16;
    assert(encoder.init_flac(cfg));
    assert(decoder.init(cfg.core_config));

    // Multi-tone test signal (16-bit integer PCM)
    int16_t orig_pcm[1152 * 2];
    for (int i = 0; i < 1152; ++i) {
        double s1 = std::sin(2.0 * constants::PI * 440.0 * i / 44100.0);
        double s2 = std::sin(2.0 * constants::PI * 1000.0 * i / 44100.0);
        orig_pcm[i * 2]     = static_cast<int16_t>(16000.0 * s1);
        orig_pcm[i * 2 + 1] = static_cast<int16_t>(16000.0 * s2);
    }

    uint8_t flac_buf[8192] = {0};
    int enc_bytes = encoder.encode_frame_i16(orig_pcm, 1152 * 2, flac_buf, sizeof(flac_buf));
    assert(enc_bytes > 0);

    int16_t dec_pcm[1152 * 2] = {0};
    int dec_samples = decoder.decode_frame_i16(flac_buf, enc_bytes, dec_pcm, 1152 * 2);
    assert(dec_samples == 1152 * 2);

    // Verify 100% BIT-EXACT equality (Lossless guarantee: diff == 0)
    for (int i = 0; i < 1152 * 2; ++i) {
        assert(dec_pcm[i] == orig_pcm[i]);
    }

    std::cout << "FLAC 100% Bit-Exact Lossless Roundtrip Verified! (Compressed to " << enc_bytes << " bytes)\n";
    return 0;
}
```

- [ ] **Step 2: Write real-world FLAC decoding test**

```cpp
// tests/test_real_flacs.cpp
#include "audio_codecs/flac/flac_decoder.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR "test-files"
#endif

bool test_flac_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << path << "\n";
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    audio_codecs::flac::FlacDecoder decoder;
    size_t header_len = 0;
    if (!decoder.parse_stream_header(buffer.data(), buffer.size(), header_len)) {
        std::cerr << "Failed to parse stream header: " << path << "\n";
        return false;
    }

    size_t offset = header_len;
    size_t frames = 0;
    size_t total_samples = 0;
    std::vector<int32_t> pcm(8192);

    while (offset + 4 <= buffer.size()) {
        int samples = decoder.decode_frame_i32(buffer.data() + offset, buffer.size() - offset, pcm.data(), pcm.size());
        if (samples <= 0) break;
        frames++;
        total_samples += samples;
        size_t advance = decoder.get_last_frame_bytes();
        if (advance == 0) advance = 4;
        offset += advance;
    }

    std::cout << "[PASS] " << path << " -> " << frames << " frames, " << total_samples << " samples ("
              << decoder.get_sample_rate() << " Hz, " << (int)decoder.get_channels() << " ch, "
              << (int)decoder.get_bit_depth() << " bit)\n";
    return (frames > 0);
}

int main() {
    std::string base = TEST_FILES_DIR;
    std::vector<std::string> files = {
        "/flac/music/music.flac",
        "/flac/music/organ.flac",
        "/flac/music/piano.flac",
        "/flac/noises/greynoise-18dB.flac",
        "/flac/noises/greynoise.flac",
        "/flac/noises/silence.flac",
        "/flac/noises/sweep.flac",
        "/flac/sine/1000Hz.flac",
        "/flac/sine/440Hz.flac",
        "/flac/sounds/click.flac"
    };

    int passed = 0;
    for (const auto& f : files) {
        if (test_flac_file(base + f)) passed++;
    }

    std::cout << "\nResult: " << passed << "/" << files.size() << " FLAC files decoded successfully!\n";
    assert(passed == static_cast<int>(files.size()));
    return 0;
}
```

- [ ] **Step 3: Update `audio_codecs.h` and `CMakeLists.txt`**
Expose `flac_decoder.h` and `flac_encoder.h` in umbrella header `audio_codecs.h` and register all test targets.

- [ ] **Step 4: Run full test suite**
Run: `ctest --test-dir build --output-on-failure`
Expected: 100% PASS across all unit, roundtrip, and real-world test suites.

- [ ] **Step 5: Commit**
```bash
git add include/audio_codecs/audio_codecs.h tests/test_flac_roundtrip.cpp tests/test_real_flacs.cpp CMakeLists.txt
git commit -m "feat(flac): complete end-to-end FLAC encoder, decoder, and integration verification"
```
