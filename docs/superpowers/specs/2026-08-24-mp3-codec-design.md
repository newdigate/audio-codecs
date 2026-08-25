# Portable MP3 Encoder & Decoder C++ Library Design Specification

**Document Date:** 2026-08-24  
**License:** MIT License (Clean-room implementation based strictly on ISO/IEC 11172-3 and ISO/IEC 13818-3 specifications; no copyleft/GPL code)  
**Target Platforms:** x86_64 / Linux, 32-bit Microcontrollers (ARM Cortex-M7 / Teensy 4.x / NXP i.MX RT1176 / ESP32) with hardware FPU.

---

## 1. Overview & Objectives

This library provides an original, high-performance, and portable C++ implementation of an **MPEG-1 / MPEG-2 / MPEG-2.5 Audio Layer III (MP3)** decoder and encoder. The architecture is designed from the ground up for multi-codec extensibility, permitting future additions (e.g., AAC, Opus, FLAC, WAV) under a unified streaming interface and clean directory layout.

### Key Objectives:
1. **Zero Runtime Dynamic Memory Allocation:** All decoding and encoding buffers, delay lines, and transform matrices are statically allocated within the codec state structures, ensuring deterministic, real-time audio processing on embedded MCUs.
2. **Standard FPU Precision:** Single-precision 32-bit floating-point (`float`) arithmetic throughout the DSP pipeline for optimal performance on Cortex-M7 hardware FPUs and desktop SIMD units.
3. **Dual Bitrate Modes:** Full support for Constant Bitrate (CBR) and Variable Bitrate (VBR) encoding.
4. **Clean Modular Structure:** Independent, testable units for Bitstream/Bit Reservoir, Huffman coding, Quantization, Forward/Inverse MDCT, Polyphase Filterbanks, and Psychoacoustics.

---

## 2. Directory Layout & Module Structure

```
audio-codecs/
├── CMakeLists.txt                 # CMake build script for library, tests, and CLI
├── include/                       # Public API headers
│   └── audio_codecs/
│       ├── audio_codecs.h         # Umbrella header
│       ├── core/
│       │   ├── audio_types.h      # ChannelMode, SampleFormat, AudioConfig, PcmView
│       │   ├── decoder_interface.h# Pure virtual AudioDecoder base class
│       │   └── encoder_interface.h# Pure virtual AudioEncoder base class
│       └── mp3/
│           ├── mp3_decoder.h      # Mp3Decoder class facade
│           └── mp3_encoder.h      # Mp3Encoder class facade
├── src/
│   ├── core/                      # Shared reusable DSP & Bitstream utilities
│   │   ├── bit_reader.h / .cpp    # MSB-first bitstream parsing
│   │   ├── bit_writer.h / .cpp    # MSB-first bitstream packing
│   │   ├── fft.h / .cpp           # In-place single-precision FFT (1024-point)
│   │   └── math_constants.h       # PI, sqrt(2), reciprocal constants
│   ├── mp3/                       # MP3 specific implementation
│   │   ├── mp3_common.h           # Header bitfields, frame constants, enums
│   │   ├── mp3_tables.h / .cpp    # Huffman tables 0-31, scalefac bands, D-window
│   │   ├── decoder/
│   │   │   ├── mp3_decoder.cpp    # Decoder orchestrator
│   │   │   ├── bit_reservoir.h/.cpp # Main data buffering & negative backpointer
│   │   │   ├── huffman_decoder.h/.cpp # Big_values & count1 quadruples decoding
│   │   │   ├── requantizer.h/.cpp # Dequantization, MS/Intensity stereo, alias reduction
│   │   │   ├── imdct.h/.cpp       # 36/12-pt IMDCT, windowing, overlap-add
│   │   │   └── synthesis_filter.h/.cpp # 32-subband polyphase synthesis filterbank
│   │   └── encoder/
│   │       ├── mp3_encoder.cpp    # Encoder orchestrator (CBR/VBR)
│   │       ├── analysis_filter.h/.cpp # 32-subband polyphase analysis filterbank
│   │       ├── mdct.h/.cpp        # Forward 36/12-pt MDCT with block switching
│   │       ├── psychoacoustic.h/.cpp # Perceptual model (FFT, Bark scale, ATH, SMR)
│   │       ├── quantizer.h/.cpp   # Inner rate loop & outer distortion loop
│   │       └── huffman_encoder.h/.cpp # Optimal table selection & code generation
│   └── (future codecs: aac/, opus/, flac/...)
└── tests/
    ├── test_bitstream.cpp
    ├── test_huffman.cpp
    ├── test_imdct.cpp
    ├── test_synthesis_filter.cpp
    ├── test_mp3_decoder.cpp
    ├── test_mp3_encoder.cpp
    └── test_roundtrip.cpp
```

---

## 3. Mathematical Foundations & DSP Pipelines

### 3.1. MP3 Decoder Pipeline & Exact Formulas

```
Input MP3 Bitstream ──► Frame Sync & Header Unpack ──► Side Info (gain, block_type, slen)
                                                             │
                                                             ▼
Reconstructed PCM ◄── Polyphase Synthesis ◄── IMDCT & ◄── Requantization, Stereo &
  (1152 samples)         Filterbank          Overlap-Add   Alias Reduction
```

#### 1. Frame Header & Bit Reservoir
* Syncword: 11 bits (`0x7FF` or 12 bits `0xFFF` in standard parsing).
* Bitstream Frame Length:
  $$\text{frame\_length\_bytes} = \left\lfloor \frac{144 \cdot \text{bitrate}}{\text{sample\_rate}} \right\rfloor + \text{padding\_bit} \quad (\text{MPEG-1})$$
  $$\text{frame\_length\_bytes} = \left\lfloor \frac{72 \cdot \text{bitrate}}{\text{sample\_rate}} \right\rfloor + \text{padding\_bit} \quad (\text{MPEG-2 / MPEG-2.5})$$
* Bit reservoir maintains up to 7680 bits (MPEG-1) / 2040 bits (MPEG-2) history to accommodate negative `main_data_begin` backpointers.

#### 2. Huffman Decoding
* Partitioning per granule/channel:
  * **Big Values:** $0 \le l < 2 \cdot \text{big\_values}$, divided into `region0`, `region1`, `region2` with distinct Huffman tables (0–31).
  * **Count1 (Quadruples):** $2 \cdot \text{big\_values} \le l < 2 \cdot \text{big\_values} + 4 \cdot \text{count1}$, coded with Table A or Table B.
  * **Rzero:** Remaining lines up to 576 set to zero.

#### 3. Requantization & Rescaling
* **Long Blocks:**
  $$xr[l] = \text{sign}(is[l]) \cdot |is[l]|^{4/3} \cdot 2^{\frac{1}{4}(\text{global\_gain} - 210)} \cdot 2^{-\text{scalefac\_multiplier} \cdot (\text{scalefac\_l}[sfb] + \text{preflag} \cdot \text{pretab}[sfb])}$$
  where $\text{scalefac\_multiplier} = 0.5$ if $\text{scalefac\_scale} == 0$ else $1.0$.  
  `pretab[22] = {0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,3,3,2,0}`.
* **Short Blocks:**
  $$xr[sfb][w][l] = \text{sign}(is[l]) \cdot |is[l]|^{4/3} \cdot 2^{\frac{1}{4}(\text{global\_gain} - 210 - 8 \cdot \text{subblock\_gain}[w])} \cdot 2^{-\text{scalefac\_multiplier} \cdot \text{scalefac\_s}[sfb][w]}$$

#### 4. Stereo Processing
* **MS Stereo:**
  $$L[l] = \frac{M[l] + S[l]}{\sqrt{2}}, \quad R[l] = \frac{M[l] - S[l]}{\sqrt{2}}$$
* **Intensity Stereo (MPEG-1):**
  $$is\_ratio = \tan\left(\frac{is\_pos[sfb] \cdot \pi}{12}\right)$$
  $$L[l] = xr_{left}[l] \cdot \frac{is\_ratio}{1 + is\_ratio}, \quad R[l] = xr_{left}[l] \cdot \frac{1}{1 + is\_ratio}$$
  *(For $is\_pos[sfb] = 7$, illegal position falls back to standard/MS stereo).*

#### 5. Alias Reduction
For long blocks, 8 butterflies per subband boundary $sb = 1 \dots 31$:
$$xar[18 \cdot sb - 1 - i] = xr[18 \cdot sb - 1 - i] \cdot Cs[i] - xr[18 \cdot sb + i] \cdot Ca[i]$$
$$xar[18 \cdot sb + i] = xr[18 \cdot sb + i] \cdot Cs[i] + xr[18 \cdot sb - 1 - i] \cdot Ca[i]$$
where $Cs[i] = \frac{1}{\sqrt{1 + c_i^2}}, \quad Ca[i] = \frac{c_i}{\sqrt{1 + c_i^2}}$, with $c_i \in \{-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037\}$.

#### 6. IMDCT & Windowing
* **Long Block IMDCT ($N=36$):**
  $$x_i = \sum_{k=0}^{17} X_k \cos\left(\frac{\pi}{72}(2i + 1 + 18)(2k + 1)\right), \quad i = 0 \dots 35$$
* **Short Block IMDCT ($N=12$):**
  $$x_i = \sum_{k=0}^{5} X_k \cos\left(\frac{\pi}{24}(2i + 1 + 6)(2k + 1)\right), \quad i = 0 \dots 11$$
* **Window Shapes:**
  * Normal: $w(i) = \sin\left(\frac{\pi}{36}(i + 0.5)\right)$ for $i=0 \dots 35$.
  * Short: $w_s(i) = \sin\left(\frac{\pi}{12}(i + 0.5)\right)$ for $i=0 \dots 11$.
  * Start & Stop transitions combine normal halves, flat 1s, and short halves.
* Overlap-add with previous block's second half (18 values) produces 18 time samples for each of the 32 subbands.

#### 7. Polyphase Synthesis Filterbank
* Invert odd time samples on odd subbands: $s[sb][t] = -s[sb][t]$ if $(sb \% 2 == 1 \land t \% 2 == 1)$.
* Subband matrixing:
  $$V[i] = \sum_{k=0}^{31} \cos\left(\frac{\pi}{64}(16 + i)(2k + 1)\right) \cdot s[k], \quad i = 0 \dots 63$$
* Shift $V$ into 1024-sample FIFO, window 512 elements by coefficients $D[0 \dots 511]$, and sum into 32 PCM output samples:
  $$X_j = \sum_{m=0}^{15} U[j + 32m], \quad j = 0 \dots 31$$

---

### 3.2. MP3 Encoder Pipeline & Algorithms

```
Input PCM Audio ──► Polyphase Analysis Filterbank (32 Subbands) ──► Forward MDCT
        │                                                                 │
        ▼                                                                 ▼
Psychoacoustic Model (1024-pt FFT, Masking, SMR) ──────────► Dual-Loop Quantization
                                                                 (CBR / VBR)
                                                                      │
                                                                      ▼
Output MP3 Stream ◄── Frame Header & Bit Reservoir ◄── Huffman Encoding
```

#### 1. Polyphase Analysis Filterbank
* 512 input PCM samples shifted into analysis FIFO.
* Windowed by analysis coefficients $C[0 \dots 511]$ and matrixed via 32-point cosine transform:
  $$S[k] = \sum_{i=0}^{511} C[i] \cdot x[i] \cdot \cos\left(\frac{\pi}{64}(2k+1)(i-16)\right), \quad k = 0 \dots 31$$

#### 2. Forward MDCT
* Windowing and 36-point (long) or 12-point (short) forward transform:
  $$X_k = \sum_{i=0}^{35} x_i \cdot w(i) \cdot \cos\left(\frac{\pi}{72}(2i + 1 + 18)(2k + 1)\right), \quad k = 0 \dots 17$$

#### 3. Psychoacoustic Model
* **Spectral Analysis:** 1024-point FFT on Hanning-windowed input signal.
* **Energy & Bark Mapping:** Group FFT bins into critical bands (Bark scale):
  $$z(f) = 13.0 \arctan(0.00076 f) + 3.5 \arctan\left(\left(\frac{f}{7500}\right)^2\right)$$
* **Masking Thresholds:** Convolve energy with spreading function:
  $$B(\Delta z) = 15.81 + 7.5(\Delta z + 0.474) - 17.5\sqrt{1 + (\Delta z + 0.474)^2} \text{ dB}$$
* **Threshold Calculation:** Compute absolute threshold of hearing (ATH), tone/noise masking, and allowable noise energy per scalefactor band ($sfb$).

#### 4. Dual-Loop Quantization (Rate & Distortion Control)
* **Non-linear Quantizer:**
  $$ix = \left\lfloor \left(\frac{|xr|}{\text{step}}\right)^{0.75} - 0.0946 \right\rfloor$$
* **Inner Loop (Rate Control):** Iteratively adjusts `global_gain` until the Huffman-encoded bit count fits the frame bit budget.
* **Outer Loop (Distortion Control):** Measures quantization noise in each scalefactor band. If noise $> \text{masking threshold}$, the band's `scalefac` is boosted, and the inner loop re-runs.
* **VBR Mode:** The bit budget is dynamically computed per frame based on perceptual entropy / SMR requirements.

---

## 4. Public C++ API Specification

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
    uint8_t vbr_quality{4}; // 0 (highest quality) to 9 (lowest)
};

template <typename T>
struct PcmView {
    T* data{nullptr};
    size_t samples_per_channel{0};
    uint8_t channels{2};
    bool interleaved{true};
};

class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;
    virtual bool init(const AudioConfig& config) = 0;
    virtual void reset() = 0;
    virtual int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                             float* out_pcm, size_t max_out_samples) = 0;
};

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

### MP3 Decoder & Encoder Facades

```cpp
#pragma once
#include "audio_codecs/core/decoder_interface.h"
#include "audio_codecs/core/encoder_interface.h"

namespace audio_codecs::mp3 {

class Mp3Decoder : public AudioDecoder {
public:
    Mp3Decoder();
    ~Mp3Decoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    bool get_frame_info(uint32_t& sample_rate, uint8_t& channels, uint32_t& bitrate_kbps) const;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[8192];
    Impl* impl_;
};

class Mp3Encoder : public AudioEncoder {
public:
    Mp3Encoder();
    ~Mp3Encoder() override;

    bool init(const AudioConfig& config) override;
    void reset() override;
    int encode_frame(const float* in_pcm, size_t in_samples,
                     uint8_t* out_data, size_t max_out_bytes) override;
    int flush(uint8_t* out_data, size_t max_out_bytes) override;

private:
    struct Impl;
    alignas(16) uint8_t state_buffer_[16384];
    Impl* impl_;
};

} // namespace audio_codecs::mp3
```

---

## 5. Embedded Memory & Safety Strategy

* **Zero Dynamic Heap Allocations:** Codec instances use pre-allocated, 16-byte aligned byte buffers (`state_buffer_`) for all internal ring buffers and transform scratch space. Placement `new` initializes internal state once during `init()`.
* **Bounded Stack Footprint:** All function call frames are strictly under 2 KB, preventing stack overflows on embedded MCU threads (FreeRTOS / Zephyr / Arduino).
* **FPU Acceleration:** Fast scalar arithmetic using single-precision `float` maps directly to ARM Cortex-M7 hardware VFP instructions (`vmla.f32`, `vfma.f32`).

---

## 6. Testing & Verification Plan

1. **Unit Tests (C++ / GoogleTest or lightweight test runner):**
   * `test_bitstream`: Bit-level read/write boundary tests, bit reservoir shift tests.
   * `test_huffman`: Table lookup accuracy, linbits decoding, quad sign bits.
   * `test_imdct`: Analytical IMDCT transform comparison against discrete cosine calculations.
   * `test_synthesis_filter`: Frequency response and perfect reconstruction tests.
2. **Integration & Regression Tests:**
   * Synthetic sine-wave test signals (440 Hz, 1 kHz, multi-tone sweeps).
   * Frame header parsing and corrupt bitstream error resilience.
   * Roundtrip test: PCM $\rightarrow$ Encoder $\rightarrow$ Decoder $\rightarrow$ PCM (verify SNR $> 35\text{ dB}$ for 128 kbps, $> 50\text{ dB}$ for 320 kbps).
