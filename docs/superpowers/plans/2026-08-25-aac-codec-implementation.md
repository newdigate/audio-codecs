# AAC-LC Codec & M4A Container Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a clean-room, MIT-licensed AAC-LC audio encoder, decoder, ADTS framer, and stream-oriented ISOBMFF/M4A demuxer/muxer for the `audio-codecs` C++17 library targeting ARM Cortex-M7 microcontrollers (Teensy 4.1, i.MX RT1176).

**Architecture:** The subsystem is divided into two modular libraries: `audio_codecs_aac` (AAC-LC DSP pipeline, MDCT filterbank, Huffman coder/decoder, psychoacoustic model, quantizer, and ADTS framing) and `audio_codecs_mp4` (stream-oriented ISOBMFF container reader and writer), linking against `audio_codecs_core`.

**Tech Stack:** C++17, CMake, CMSIS-DSP/FPU-friendly single-precision float math, zero dynamic allocation in hot loops.

**Spec:** [`docs/superpowers/specs/2026-08-25-aac-codec-design.md`](file:///Users/moolet/Development/github/newdigate/audio-codecs/docs/superpowers/specs/2026-08-25-aac-codec-design.md)

## Global Constraints
- Target standard: ISO/IEC 13818-7 and ISO/IEC 14496-3 Subpart 4 (AAC-LC profile).
- Language standard: C++17 (`-std=c++17`, no external dependencies beyond stdlib and `audio_codecs_core`).
- Memory policy: No dynamic heap allocation in per-frame decode/encode functions; all buffers static or instance-owned.
- Precision: Single-precision `float` (`float32_t`) for all audio sample processing and spectral transforms.

---

### Task 1: Core AAC Tables & Window Generation

**Files:**
- Create: `include/audio_codecs/aac/aac_types.h`
- Create: `src/aac/aac_tables.h`
- Create: `src/aac/aac_tables.cpp`
- Test: `tests/test_aac_tables.cpp`

**Interfaces:**
- Consumes: Standard math headers (`<cmath>`, `<cstdint>`)
- Produces:
  - `enum class WindowSequence { OnlyLong = 0, LongStart = 1, EightShort = 2, LongStop = 3 };`
  - `enum class WindowShape { Sine = 0, KBD = 1 };`
  - `const int* get_swb_offset_long(uint32_t sample_rate, size_t& num_bands);`
  - `const int* get_swb_offset_short(uint32_t sample_rate, size_t& num_bands);`
  - `const float* get_sine_window_1024();`
  - `const float* get_sine_window_128();`
  - `const float* get_kbd_window_1024();`
  - `const float* get_kbd_window_128();`
  - `float dequant_pow43(int val);` (LUT for $0 \le val \le 256$)

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aac_tables.cpp
#include "src/aac/aac_tables.h"
#include "include/audio_codecs/aac/aac_types.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::aac;

    // Verify 44.1kHz scalefactor band table
    size_t num_bands = 0;
    const int* swb_44100 = get_swb_offset_long(44100, num_bands);
    assert(swb_44100 != nullptr);
    assert(num_bands == 49);
    assert(swb_44100[0] == 0);
    assert(swb_44100[num_bands] == 1024);

    // Verify short window bands
    const int* swb_short = get_swb_offset_short(44100, num_bands);
    assert(swb_short != nullptr);
    assert(num_bands == 14);
    assert(swb_short[0] == 0);
    assert(swb_short[num_bands] == 128);

    // Verify window symmetry and normalization: w[n]^2 + w[n + N]^2 == 1 for Princen-Bradley
    const float* sine_1024 = get_sine_window_1024();
    assert(sine_1024 != nullptr);
    for (int i = 0; i < 1024; ++i) {
        float val = sine_1024[i] * sine_1024[i] + sine_1024[2048 - 1 - i] * sine_1024[2048 - 1 - i];
        assert(std::fabs(val - 1.0f) < 1e-4f);
    }

    // Verify pow43 LUT
    assert(std::fabs(dequant_pow43(0) - 0.0f) < 1e-5f);
    assert(std::fabs(dequant_pow43(1) - 1.0f) < 1e-5f);
    assert(std::fabs(dequant_pow43(8) - 16.0f) < 1e-4f); // 8^(4/3) = 16

    std::cout << "AAC Tables test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `aac_types.h`, `aac_tables.h`, and `aac_tables.cpp`**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aac/aac_types.h src/aac/aac_tables.h src/aac/aac_tables.cpp tests/test_aac_tables.cpp
git commit -m "feat(aac): implement scalefactor tables, KBD/Sine windows, and pow43 LUT"
```

---

### Task 2: AAC Forward and Inverse MDCT Filterbank

**Files:**
- Create: `src/aac/aac_mdct.h`
- Create: `src/aac/aac_mdct.cpp`
- Test: `tests/test_aac_mdct.cpp`

**Interfaces:**
- Consumes: `src/core/fft.h`, `src/aac/aac_tables.h`
- Produces:
  - `class AacMdct { public: void forward_long(const float* in_time_2048, float* out_freq_1024, WindowShape shape = WindowShape::Sine); void inverse_long(const float* in_freq_1024, float* out_time_2048, WindowShape shape = WindowShape::Sine); void forward_short(const float* in_time_256, float* out_freq_128, WindowShape shape = WindowShape::Sine); void inverse_short(const float* in_freq_128, float* out_time_256, WindowShape shape = WindowShape::Sine); };`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aac_mdct.cpp
#include "src/aac/aac_mdct.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::aac;

    AacMdct mdct;

    // Test Time-Domain Alias Cancellation (TDAC) on 2 consecutive frames
    // Frame 0: samples 0..2047
    // Frame 1: samples 1024..3071
    std::vector<float> original_signal(3072);
    for (size_t i = 0; i < original_signal.size(); ++i) {
        original_signal[i] = std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
    }

    float freq0[1024] = {0.0f};
    float freq1[1024] = {0.0f};
    mdct.forward_long(&original_signal[0], freq0, WindowShape::Sine);
    mdct.forward_long(&original_signal[1024], freq1, WindowShape::Sine);

    float time0[2048] = {0.0f};
    float time1[2048] = {0.0f};
    mdct.inverse_long(freq0, time0, WindowShape::Sine);
    mdct.inverse_long(freq1, time1, WindowShape::Sine);

    // Overlap-add second half of frame 0 with first half of frame 1:
    // time0[1024..2047] + time1[0..1023] should equal original_signal[1024..2047]
    for (int i = 0; i < 1024; ++i) {
        float reconstructed = time0[1024 + i] + time1[i];
        float orig = original_signal[1024 + i];
        assert(std::fabs(reconstructed - orig) < 1e-4f);
    }

    std::cout << "AAC MDCT TDAC test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `src/aac/aac_mdct.h` and `src/aac/aac_mdct.cpp` using fast complex FFT**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add src/aac/aac_mdct.h src/aac/aac_mdct.cpp tests/test_aac_mdct.cpp
git commit -m "feat(aac): implement forward and inverse MDCT with TDAC"
```

---

### Task 3: ADTS Header & Bitstream Framing

**Files:**
- Create: `include/audio_codecs/aac/adts_header.h`
- Create: `src/aac/adts_parser.h`
- Create: `src/aac/adts_parser.cpp`
- Test: `tests/test_adts_framing.cpp`

**Interfaces:**
- Consumes: `include/audio_codecs/core/audio_types.h`, `src/core/bit_reader.h`, `src/core/bit_writer.h`
- Produces:
  - `struct AdtsHeader { uint8_t profile{1}; uint32_t sample_rate{44100}; uint8_t sample_rate_index{4}; uint8_t channel_config{2}; bool protection_absent{true}; uint16_t frame_length{0}; uint16_t raw_data_block_bytes{0}; };`
  - `bool parse_adts_header(core::BitReader& reader, AdtsHeader& header);`
  - `size_t write_adts_header(core::BitWriter& writer, const AdtsHeader& header);`
  - `uint16_t calculate_adts_crc(const uint8_t* frame_data, size_t header_plus_raw_bytes);`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_adts_framing.cpp
#include "include/audio_codecs/aac/adts_header.h"
#include "src/aac/adts_parser.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    AdtsHeader hdr_in;
    hdr_in.profile = 1; // AAC-LC
    hdr_in.sample_rate = 48000;
    hdr_in.sample_rate_index = 3;
    hdr_in.channel_config = 2;
    hdr_in.protection_absent = true;
    hdr_in.frame_length = 7 + 120; // 7 bytes header + 120 bytes payload

    std::vector<uint8_t> buffer(256, 0);
    BitWriter writer(buffer.data(), buffer.size());
    size_t written_bits = write_adts_header(writer, hdr_in);
    assert(written_bits == 56); // 7 bytes

    BitReader reader;
    reader.init(buffer.data(), buffer.size());
    AdtsHeader hdr_out;
    assert(parse_adts_header(reader, hdr_out));

    assert(hdr_out.profile == 1);
    assert(hdr_out.sample_rate == 48000);
    assert(hdr_out.channel_config == 2);
    assert(hdr_out.frame_length == 127);
    assert(hdr_out.protection_absent == true);

    std::cout << "ADTS framing test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `adts_header.h`, `adts_parser.h`, and `adts_parser.cpp`**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aac/adts_header.h src/aac/adts_parser.h src/aac/adts_parser.cpp tests/test_adts_framing.cpp
git commit -m "feat(aac): implement ADTS header packing, unpacking and CRC verification"
```

---

### Task 4: AAC Huffman Spectral Decoder & Requantizer

**Files:**
- Create: `src/aac/decoder/huffman_decoder.h`
- Create: `src/aac/decoder/huffman_decoder.cpp`
- Create: `src/aac/decoder/requantizer.h`
- Create: `src/aac/decoder/requantizer.cpp`
- Test: `tests/test_aac_huffman.cpp`

**Interfaces:**
- Consumes: `src/core/bit_reader.h`, `src/aac/aac_tables.h`
- Produces:
  - `bool decode_spectral_data(core::BitReader& reader, int codebook, int* out_quad_or_pair, int count);`
  - `void requantize_spectrum(const int* quant_spectral, const int* scalefactors, const int* swb_offsets, size_t num_swb, float* out_float_spectral);`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_aac_huffman.cpp
#include "src/aac/decoder/huffman_decoder.h"
#include "src/aac/decoder/requantizer.h"
#include "src/aac/aac_tables.h"
#include "src/core/bit_writer.h"
#include "src/core/bit_reader.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    // Test requantizer with known formula: X_inv = sign(X) * |X|^(4/3) * 2^(0.25 * (sf - 100))
    int quant[4] = {0, 1, 8, -8};
    int sf[1] = {100}; // 2^(0) = 1.0
    int swb[2] = {0, 4};
    float out[4] = {0.0f};

    requantize_spectrum(quant, sf, swb, 1, out);
    assert(std::fabs(out[0] - 0.0f) < 1e-4f);
    assert(std::fabs(out[1] - 1.0f) < 1e-4f);
    assert(std::fabs(out[2] - 16.0f) < 1e-4f);
    assert(std::fabs(out[3] - (-16.0f)) < 1e-4f);

    std::cout << "AAC Huffman & Requantizer test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `huffman_decoder.cpp` and `requantizer.cpp`**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add src/aac/decoder/huffman_decoder.h src/aac/decoder/huffman_decoder.cpp src/aac/decoder/requantizer.h src/aac/decoder/requantizer.cpp tests/test_aac_huffman.cpp
git commit -m "feat(aac): implement Huffman spectral decoding and non-linear requantization"
```

---

### Task 5: Joint Stereo (M/S, IS), PNS, and TNS Decoders

**Files:**
- Create: `src/aac/decoder/stereo_processor.h`
- Create: `src/aac/decoder/stereo_processor.cpp`
- Create: `src/aac/decoder/tns_decoder.h`
- Create: `src/aac/decoder/tns_decoder.cpp`
- Test: `tests/test_aac_stereo_tns.cpp`

**Interfaces:**
- Consumes: `src/aac/aac_tables.h`
- Produces:
  - `void apply_ms_stereo(float* left_spec, float* right_spec, const bool* ms_used, const int* swb_offsets, size_t num_swb);`
  - `void apply_tns_filter(float* spec, const TnsData& tns_data, const int* swb_offsets, size_t num_swb);`

- [x] **Step 1: Write the failing test**
- [x] **Step 2: Run test to verify it fails**
- [x] **Step 3: Implement stereo processor and TNS filter**
- [x] **Step 4: Run test to verify it passes**
- [x] **Step 5: Commit**

```bash
git add src/aac/decoder/stereo_processor.h src/aac/decoder/stereo_processor.cpp src/aac/decoder/tns_decoder.h src/aac/decoder/tns_decoder.cpp tests/test_aac_stereo_tns.cpp
git commit -m "feat(aac): implement M/S stereo, intensity stereo and TNS filter"
```

---

### Task 6: AAC-LC Decoder (`AudioDecoder` implementation)

**Files:**
- Create: `include/audio_codecs/aac/aac_decoder.h`
- Create: `src/aac/decoder/aac_decoder.cpp`
- Test: `tests/test_aac_decoder.cpp`

**Interfaces:**
- Consumes: `include/audio_codecs/core/decoder_interface.h`, `src/aac/adts_parser.h`, `src/aac/aac_mdct.h`, `src/aac/decoder/huffman_decoder.h`, `src/aac/decoder/requantizer.h`, `src/aac/decoder/stereo_processor.h`
- Produces:
  - `class AacDecoder : public AudioDecoder { public: bool init(const AudioConfig& config) override; void reset() override; int decode_frame(const uint8_t* in_data, size_t in_bytes, float* out_pcm, size_t max_out_samples) override; };`

- [ ] **Step 1: Write the failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `AacDecoder`**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aac/aac_decoder.h src/aac/decoder/aac_decoder.cpp tests/test_aac_decoder.cpp
git commit -m "feat(aac): implement AacDecoder facade complying with AudioDecoder interface"
```

---

### Task 7: AAC-LC Encoder: Transient Detection, Psychoacoustics & Quantizer

**Files:**
- Create: `src/aac/encoder/transient_detector.h`
- Create: `src/aac/encoder/transient_detector.cpp`
- Create: `src/aac/encoder/psychoacoustic.h`
- Create: `src/aac/encoder/psychoacoustic.cpp`
- Create: `src/aac/encoder/quantizer.h`
- Create: `src/aac/encoder/quantizer.cpp`
- Create: `src/aac/encoder/huffman_encoder.h`
- Create: `src/aac/encoder/huffman_encoder.cpp`
- Test: `tests/test_aac_encoder_components.cpp`

**Interfaces:**
- Consumes: `src/core/fft.h`, `src/core/bit_writer.h`, `src/aac/aac_tables.h`
- Produces:
  - `WindowSequence detect_transient(const float* pcm_2048, float& energy_ratio);`
  - `void calculate_masking_thresholds(const float* pcm_2048, uint32_t sample_rate, float* out_thresholds_swb, size_t num_swb);`
  - `void quantize_spectrum_fast(const float* in_spectral, const float* masking_thresholds, const int* swb_offsets, size_t num_swb, int* out_quant, int* out_scalefactors, int target_bits);`
  - `size_t encode_spectral_huffman(core::BitWriter& writer, const int* quant_spectral, const int* swb_offsets, size_t num_swb);`

- [ ] **Step 1: Write the failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement encoder DSP modules**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add src/aac/encoder/transient_detector.* src/aac/encoder/psychoacoustic.* src/aac/encoder/quantizer.* src/aac/encoder/huffman_encoder.* tests/test_aac_encoder_components.cpp
git commit -m "feat(aac): implement encoder psychoacoustic model, quantizer and Huffman packer"
```

---

### Task 8: Complete AAC-LC Encoder & Roundtrip Verification

**Files:**
- Create: `include/audio_codecs/aac/aac_encoder.h`
- Create: `src/aac/encoder/aac_encoder.cpp`
- Test: `tests/test_aac_encoder.cpp`
- Test: `tests/test_aac_roundtrip.cpp`

**Interfaces:**
- Consumes: `include/audio_codecs/core/encoder_interface.h`, `include/audio_codecs/aac/aac_decoder.h`
- Produces:
  - `class AacEncoder : public AudioEncoder { public: bool init(const AudioConfig& config) override; void reset() override; int encode_frame(const float* in_pcm, size_t in_samples, uint8_t* out_data, size_t max_out_bytes) override; int flush(uint8_t* out_data, size_t max_out_bytes) override; };`

- [ ] **Step 1: Write the failing roundtrip test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `AacEncoder`**
- [ ] **Step 4: Run test to verify roundtrip passes with SNR > 60 dB**
- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/aac/aac_encoder.h src/aac/encoder/aac_encoder.cpp tests/test_aac_encoder.cpp tests/test_aac_roundtrip.cpp
git commit -m "feat(aac): implement AacEncoder and verify full encode-decode roundtrip"
```

---

### Task 9: Stream-Oriented ISOBMFF / M4A Demuxer & Muxer (`audio_codecs_mp4`)

**Files:**
- Create: `include/audio_codecs/mp4/mp4_types.h`
- Create: `include/audio_codecs/mp4/mp4_demuxer.h`
- Create: `include/audio_codecs/mp4/mp4_muxer.h`
- Create: `src/mp4/mp4_demuxer.cpp`
- Create: `src/mp4/mp4_muxer.cpp`
- Test: `tests/test_mp4_container.cpp`

**Interfaces:**
- Consumes: Standard streams and `include/audio_codecs/core/audio_types.h`
- Produces:
  - `class Mp4Demuxer { public: bool open(const uint8_t* data, size_t size); bool get_audio_config(AudioConfig& config, std::vector<uint8_t>& asc); bool read_next_sample(const uint8_t*& sample_ptr, size_t& sample_size); };`
  - `class Mp4Muxer { public: bool init(const AudioConfig& config, const std::vector<uint8_t>& asc); bool add_sample(const uint8_t* sample_data, size_t sample_size); std::vector<uint8_t> finalize(); };`

- [ ] **Step 1: Write the failing test**
- [ ] **Step 2: Run test to verify it fails**
- [ ] **Step 3: Implement `Mp4Demuxer` and `Mp4Muxer`**
- [ ] **Step 4: Run test to verify it passes**
- [ ] **Step 5: Commit**

```bash
git add include/audio_codecs/mp4/ src/mp4/ tests/test_mp4_container.cpp
git commit -m "feat(mp4): implement stream-oriented M4A ISOBMFF demuxer and muxer"
```

---

### Task 10: Umbrella Headers, CMake Integration & Complete Test Run

**Files:**
- Create: `include/audio_codecs/aac.h`
- Create: `include/audio_codecs/mp4.h`
- Modify: `include/audio_codecs/audio_codecs.h`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Update `CMakeLists.txt` to add `audio_codecs_aac` and `audio_codecs_mp4` libraries and all test executables**
- [ ] **Step 2: Build the entire project and run `ctest --output-on-failure`**
- [ ] **Step 3: Verify 100% passing tests**
- [ ] **Step 4: Commit**

```bash
git add include/audio_codecs/aac.h include/audio_codecs/mp4.h include/audio_codecs/audio_codecs.h CMakeLists.txt
git commit -m "build: integrate audio_codecs_aac and audio_codecs_mp4 into CMake build"
```
