# Portable MP3 Encoder & Decoder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an original, high-performance, clean-room C++ MP3 encoder and decoder library under the MIT License targeting x86_64/Linux and 32-bit MCUs (Teensy 4.x / i.MX RT1176) with zero runtime dynamic allocations.

**Architecture:** A modular audio codec architecture where common stream interfaces and core DSP primitives (FFT, bitstream reader/writer) reside in `src/core/`, while format-specific decoding, encoding, table definitions, and psychoacoustics reside in `src/mp3/`, structured for future codec additions.

**Tech Stack:** Modern C++17, CMake 3.16+, CTest, standard FPU single-precision (`float`).

**Spec:** `docs/superpowers/specs/2026-08-24-mp3-codec-design.md`

## Global Constraints
- Clean-room implementation derived strictly from ISO/IEC 11172-3 and ISO/IEC 13818-3 specifications; no copyleft/GPL/LGPL references or code.
- Zero runtime heap allocation (`malloc`, `free`, `new`, `delete`) in the streaming decode/encode frame path.
- Standard single-precision `float` operations optimized for 32-bit ARM Cortex-M7 hardware FPU and desktop CPUs.
- Codec directory isolation under `src/mp3/` and public headers under `include/audio_codecs/`.

---

### Task 1: Core Framework, CMake Build System, and Audio Types

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/audio_codecs/audio_codecs.h`
- Create: `include/audio_codecs/core/audio_types.h`
- Create: `include/audio_codecs/core/decoder_interface.h`
- Create: `include/audio_codecs/core/encoder_interface.h`
- Create: `src/core/math_constants.h`
- Create: `tests/test_core_types.cpp`

**Interfaces:**
- Produces: `audio_codecs::AudioConfig`, `audio_codecs::PcmView<T>`, `audio_codecs::AudioDecoder`, `audio_codecs::AudioEncoder`, `audio_codecs::constants::*`.

- [ ] **Step 1: Write the failing test for Core Audio Types & Interfaces**

```cpp
// tests/test_core_types.cpp
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/core/encoder_interface.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <iostream>
#include <cmath>

int main() {
    using namespace audio_codecs;
    AudioConfig config{44100, 2, 128, false, 4};
    assert(config.sample_rate == 44100);
    assert(config.channels == 2);
    assert(config.bitrate_kbps == 128);
    assert(!config.vbr);

    float dummy_buffer[10] = {0.0f};
    PcmView<float> view{dummy_buffer, 5, 2, true};
    assert(view.samples_per_channel == 5);
    assert(view.channels == 2);

    assert(std::fabs(constants::PI - 3.14159265358979323846f) < 1e-6f);
    assert(std::fabs(constants::SQRT2 - 1.41421356237309504880f) < 1e-6f);
    assert(std::fabs(constants::INV_SQRT2 - 0.70710678118654752440f) < 1e-6f);

    std::cout << "Core types test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -B build && cmake --build build --target test_core_types`  
Expected: FAIL (missing files).

- [ ] **Step 3: Implement minimal headers, constants, and CMakeLists.txt**

Create `include/audio_codecs/core/audio_types.h`:
```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace audio_codecs {

enum class SampleFormat {
    Float32,
    Int16
};

enum class ChannelMode {
    Mono = 1,
    Stereo = 2
};

struct AudioConfig {
    uint32_t sample_rate{44100};
    uint8_t channels{2};
    uint32_t bitrate_kbps{128};
    bool vbr{false};
    uint8_t vbr_quality{4}; // 0 (highest) to 9 (lowest)
};

template <typename T>
struct PcmView {
    T* data{nullptr};
    size_t samples_per_channel{0};
    uint8_t channels{2};
    bool interleaved{true};
};

} // namespace audio_codecs
```

Create `include/audio_codecs/core/decoder_interface.h`:
```cpp
#pragma once
#include "audio_codecs/core/audio_types.h"

namespace audio_codecs {

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    virtual bool init(const AudioConfig& config) = 0;
    virtual void reset() = 0;
    virtual int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                             float* out_pcm, size_t max_out_samples) = 0;
};

} // namespace audio_codecs
```

Create `include/audio_codecs/core/encoder_interface.h`:
```cpp
#pragma once
#include "audio_codecs/core/audio_types.h"

namespace audio_codecs {

class AudioEncoder {
public:
    virtual ~AudioEncoder() = default;
    virtual bool init(const AudioConfig& config) = 0;
    virtual void reset() = 0;
    virtual int encode_frame(const float* in_pcm, size_t in_samples,
                             uint8_t* out_data, size_t max_out_bytes) = 0;
    virtual int flush(uint8_t* out_data, size_t max_out_bytes) = 0;
};

} // namespace audio_codecs
```

Create `src/core/math_constants.h`:
```cpp
#pragma once

namespace audio_codecs::constants {

inline constexpr float PI = 3.141592653589793238462643383279502884f;
inline constexpr float TWO_PI = 6.283185307179586476925286766559005768f;
inline constexpr float SQRT2 = 1.414213562373095048801688724209698078f;
inline constexpr float INV_SQRT2 = 0.707106781186547524400844362104849039f;

} // namespace audio_codecs::constants
```

Create `include/audio_codecs/audio_codecs.h`:
```cpp
#pragma once
#include "audio_codecs/core/audio_types.h"
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/core/encoder_interface.h"
```

Create `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(audio_codecs VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()

add_executable(test_core_types tests/test_core_types.cpp)
add_test(NAME CoreTypesTest COMMAND test_core_types)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`  
Expected: `1/1 Test #1: CoreTypesTest ... Passed`

---

### Task 2: Fast Bit Reader, Bit Writer, and 1024-point FFT Engine

**Files:**
- Create: `src/core/bit_reader.h`
- Create: `src/core/bit_reader.cpp`
- Create: `src/core/bit_writer.h`
- Create: `src/core/bit_writer.cpp`
- Create: `src/core/fft.h`
- Create: `src/core/fft.cpp`
- Create: `tests/test_bitstream.cpp`
- Create: `tests/test_fft.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::core::BitReader`: `init(const uint8_t* data, size_t num_bytes)`, `read_bits(int n)`, `peek_bits(int n)`, `skip_bits(int n)`, `bits_remaining() const`, `get_position_bits() const`, `set_position_bits(size_t pos)`.
- `audio_codecs::core::BitWriter`: `init(uint8_t* buffer, size_t max_bytes)`, `write_bits(uint32_t value, int n)`, `flush_to_byte()`, `get_bit_count() const`, `get_byte_count() const`.
- `audio_codecs::core::Fft1024`: `init()`, `transform_real(const float* time_in, float* real_out, float* imag_out)`.

- [ ] **Step 1: Write the failing tests for Bitstream & FFT**

```cpp
// tests/test_bitstream.cpp
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::core;
    uint8_t buffer[16] = {0};
    BitWriter writer;
    writer.init(buffer, sizeof(buffer));
    
    // Write 11-bit syncword 0x7FF, 2-bit layer 0x01, 1-bit protection 0x01, 4-bit bitrate 0x09
    writer.write_bits(0x7FF, 11);
    writer.write_bits(0x01, 2);
    writer.write_bits(0x01, 1);
    writer.write_bits(0x09, 4);
    writer.flush_to_byte();

    assert(writer.get_bit_count() == 18);
    assert(writer.get_byte_count() == 3);

    BitReader reader;
    reader.init(buffer, writer.get_byte_count());
    assert(reader.read_bits(11) == 0x7FF);
    assert(reader.read_bits(2) == 0x01);
    assert(reader.read_bits(1) == 0x01);
    assert(reader.read_bits(4) == 0x09);
    assert(reader.bits_remaining() == 6);

    std::cout << "Bitstream tests passed!\n";
    return 0;
}
```

```cpp
// tests/test_fft.cpp
#include "src/core/fft.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::core;
    Fft1024 fft;
    fft.init();

    float time_in[1024] = {0.0f};
    float real_out[513] = {0.0f};
    float imag_out[513] = {0.0f};

    // 16 cycles in 1024 samples (bin 16)
    for (int i = 0; i < 1024; ++i) {
        time_in[i] = std::cos(audio_codecs::constants::TWO_PI * 16.0f * i / 1024.0f);
    }

    fft.transform_real(time_in, real_out, imag_out);

    float power16 = real_out[16]*real_out[16] + imag_out[16]*imag_out[16];
    float power0 = real_out[0]*real_out[0] + imag_out[0]*imag_out[0];
    float power15 = real_out[15]*real_out[15] + imag_out[15]*imag_out[15];

    assert(power16 > 1000.0f);
    assert(power0 < 0.1f);
    assert(power15 < 0.1f);

    std::cout << "FFT tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target test_bitstream test_fft`  
Expected: FAIL.

- [ ] **Step 3: Implement BitReader, BitWriter, and radix-2 Cooley-Tukey FFT1024**

Implement MSB-first bit operations and fast table-driven in-place FFT with zero runtime allocations.

- [ ] **Step 4: Run tests to verify they pass**

Run: `ctest --test-dir build --output-on-failure`  
Expected: All tests pass.

---

### Task 3: MP3 Common Headers, Data Structures, and Static Tables

**Files:**
- Create: `src/mp3/mp3_common.h`
- Create: `src/mp3/mp3_tables.h`
- Create: `src/mp3/mp3_tables.cpp`
- Create: `tests/test_mp3_tables.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `MpegVersion` (Mpeg1, Mpeg2, Mpeg25), `MpegLayer` (Layer3), `MpegMode` (Stereo, JointStereo, DualChannel, SingleChannel).
  - `FrameHeader` struct: parse from 32-bit word, calculate frame length, slot size, and sample rate.
  - `SideInfo` struct: `main_data_begin`, `scfsi[2][4]`, `granules[2][2]` with `part2_3_length`, `big_values`, `global_gain`, `scalefac_compress`, `block_type`, `table_select[3]`, etc.
  - Tables: `HUFFMAN_TABLES[32]`, `COUNT1_TABLES[2]`, `SCALEFAC_BANDS_LONG[9][23]`, `SCALEFAC_BANDS_SHORT[9][14]`, `D_SYNTHESIS_WINDOW[512]`, `C_ANALYSIS_WINDOW[512]`, `PRETAB[22]`, `ALIAS_CS[8]`, `ALIAS_CA[8]`.

- [ ] **Step 1: Write test for header parsing & table integrity**

```cpp
// tests/test_mp3_tables.cpp
#include "src/mp3/mp3_common.h"
#include "src/mp3/mp3_tables.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::mp3;
    // 0xFFFB9064: 44.1kHz, 128kbps, Stereo, MPEG-1 Layer 3, no CRC, no padding
    uint32_t header_word = 0xFFFB9064;
    FrameHeader header;
    bool ok = parse_frame_header(header_word, header);
    assert(ok);
    assert(header.version == MpegVersion::Mpeg1);
    assert(header.layer == MpegLayer::Layer3);
    assert(header.bitrate_kbps == 128);
    assert(header.sample_rate == 44100);
    assert(header.channels == 2);
    assert(header.frame_bytes == 417);

    // Verify D[512] synthesis window symmetry and values
    assert(D_SYNTHESIS_WINDOW[0] == 0.0f);
    assert(std::fabs(D_SYNTHESIS_WINDOW[256] - 1.144989014f) < 1e-5f);

    // Verify pretab table values
    assert(PRETAB[0] == 0);
    assert(PRETAB[11] == 1);
    assert(PRETAB[15] == 2);
    assert(PRETAB[17] == 3);

    std::cout << "MP3 tables and header parsing test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Implement mp3_common and complete exact static tables**
- [ ] **Step 3: Run test and verify it passes**

---

### Task 4: MP3 Bit Reservoir & Huffman Decoder

**Files:**
- Create: `src/mp3/decoder/bit_reservoir.h`
- Create: `src/mp3/decoder/bit_reservoir.cpp`
- Create: `src/mp3/decoder/huffman_decoder.h`
- Create: `src/mp3/decoder/huffman_decoder.cpp`
- Create: `tests/test_huffman_decoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::BitReservoir`: `push_bytes(const uint8_t* src, size_t bytes)`, `get_reader(size_t main_data_begin, size_t main_data_len)`.
- `audio_codecs::mp3::HuffmanDecoder`: `decode_granule(BitReader& reader, const GranuleInfo& gi, int16_t* is_out_576)`.

- [ ] **Step 1: Write failing test for Bit Reservoir & Huffman decoding**
- [ ] **Step 2: Implement BitReservoir circular buffer and Huffman tree decoder**
- [ ] **Step 3: Verify with known test vectors**

---

### Task 5: MP3 Dequantizer, Stereo Processing, and Alias Reduction

**Files:**
- Create: `src/mp3/decoder/requantizer.h`
- Create: `src/mp3/decoder/requantizer.cpp`
- Create: `tests/test_requantizer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::Requantizer`:
  - `decode_scalefactors(BitReader& reader, const FrameHeader& header, const SideInfo& side, int gr, int ch, ScalefactorData& sf)`.
  - `requantize_granule(const int16_t* is, const ScalefactorData& sf, const GranuleInfo& gi, const FrameHeader& header, float* xr_576)`.
  - `process_stereo(float* xr_left, float* xr_right, const GranuleInfo& gi_left, const GranuleInfo& gi_right, const FrameHeader& header)`.
  - `alias_reduction(float* xr, const GranuleInfo& gi)`.

- [ ] **Step 1: Write failing test for Requantizer, MS/Intensity Stereo, and Alias Butterflies**
- [ ] **Step 2: Implement exact requantization formulas and alias reduction**
- [ ] **Step 3: Verify precision against analytical expected values**

---

### Task 6: MP3 IMDCT, Windowing, Overlap-Add, and Polyphase Synthesis Filterbank

**Files:**
- Create: `src/mp3/decoder/imdct.h`
- Create: `src/mp3/decoder/imdct.cpp`
- Create: `src/mp3/decoder/synthesis_filter.h`
- Create: `src/mp3/decoder/synthesis_filter.cpp`
- Create: `tests/test_imdct.cpp`
- Create: `tests/test_synthesis_filter.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::ImdctEngine`: `transform_block(const float* xr_18, int block_type, float* overlap_18, float* out_18)`.
- `audio_codecs::mp3::SynthesisFilter`: `reset()`, `filter_subband_samples(const float* s_32, float* out_pcm_32)`.

- [ ] **Step 1: Write failing tests for 36/12-pt IMDCT and 32-subband Synthesis Polyphase**
- [ ] **Step 2: Implement matrixing, windowing, and overlap-add delay lines**
- [ ] **Step 3: Verify perfect reconstruction and subband impulse response**

---

### Task 7: MP3 Decoder Facade & End-to-End Decoder Verification

**Files:**
- Create: `include/audio_codecs/mp3/mp3_decoder.h`
- Create: `src/mp3/decoder/mp3_decoder.cpp`
- Create: `tests/test_mp3_decoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::Mp3Decoder` public class implementing `AudioDecoder`.

- [ ] **Step 1: Write failing test for Mp3Decoder on valid synthesized MP3 frames**
- [ ] **Step 2: Implement full decode orchestrator coordinating reservoir, Huffman, requantizer, IMDCT, and synthesis filterbank**
- [ ] **Step 3: Run end-to-end decode tests and confirm bit-exact synchronization**

---

### Task 8: MP3 Analysis Polyphase Filterbank & Forward MDCT

**Files:**
- Create: `src/mp3/encoder/analysis_filter.h`
- Create: `src/mp3/encoder/analysis_filter.cpp`
- Create: `src/mp3/encoder/mdct.h`
- Create: `src/mp3/encoder/mdct.cpp`
- Create: `tests/test_analysis_filter.cpp`
- Create: `tests/test_forward_mdct.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::AnalysisFilter`: `filter_pcm(const float* pcm_32, float* out_subband_32)`.
- `audio_codecs::mp3::ForwardMdct`: `transform_block(const float* subband_time_36, int block_type, float* out_mdct_18)`.

- [ ] **Step 1: Write failing tests for Analysis Filterbank and Forward MDCT**
- [ ] **Step 2: Implement forward cosine matrix and 512-sample analysis windowing**
- [ ] **Step 3: Verify analysis followed by synthesis preserves original audio**

---

### Task 9: Psychoacoustic Model & Perceptual Thresholds

**Files:**
- Create: `src/mp3/encoder/psychoacoustic.h`
- Create: `src/mp3/encoder/psychoacoustic.cpp`
- Create: `tests/test_psychoacoustic.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::PsychoacousticModel`: `calculate_masking(const float* pcm_1024, uint32_t sample_rate, float* mask_thresholds_sfb, float* smr_sfb)`.

- [ ] **Step 1: Write failing test for spectral energy, ATH, and masking thresholds**
- [ ] **Step 2: Implement FFT-based Bark spreading and Signal-to-Mask Ratio calculation**
- [ ] **Step 3: Verify masking curve on single-tone and noise inputs**

---

### Task 10: MP3 Rate-Distortion Quantization (CBR & VBR) & Huffman Encoder

**Files:**
- Create: `src/mp3/encoder/quantizer.h`
- Create: `src/mp3/encoder/quantizer.cpp`
- Create: `src/mp3/encoder/huffman_encoder.h`
- Create: `src/mp3/encoder/huffman_encoder.cpp`
- Create: `tests/test_huffman_encoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::Quantizer`: `quantize_cbr(...)`, `quantize_vbr(...)`.
- `audio_codecs::mp3::HuffmanEncoder`: `choose_optimal_table(const int16_t* is, int len, int& table_num, int& linbits)`, `encode_region(BitWriter& writer, const int16_t* is, int len, int table_num)`.

- [ ] **Step 1: Write failing test for rate loop convergence and Huffman table encoding**
- [ ] **Step 2: Implement inner bit-budget loop, outer perceptual distortion loop, and optimal Huffman table selector**
- [ ] **Step 3: Verify encoded bitstream correctly unpacks with HuffmanDecoder**

---

### Task 11: MP3 Encoder Facade, Bit Reservoir Packing, and End-to-End Roundtrip Tests

**Files:**
- Create: `include/audio_codecs/mp3/mp3_encoder.h`
- Create: `src/mp3/encoder/mp3_encoder.cpp`
- Create: `tests/test_mp3_encoder.cpp`
- Create: `tests/test_roundtrip.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `audio_codecs::mp3::Mp3Encoder` public class implementing `AudioEncoder`.

- [ ] **Step 1: Write failing test for complete PCM -> Encode -> Bitstream -> Decode -> PCM roundtrip**
- [ ] **Step 2: Implement Mp3Encoder facade with CBR and VBR modes and bit reservoir output framing**
- [ ] **Step 3: Run automated roundtrip tests across 32k/44.1k/48kHz and 64k-320kbps bitrates, asserting reconstruction SNR $> 35\text{ dB}$**
- [ ] **Step 4: Verify zero dynamic memory allocations during runtime streaming**
