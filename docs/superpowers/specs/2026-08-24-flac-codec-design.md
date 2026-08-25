# FLAC Audio Codec (Encoder & Decoder) Design Specification

**Specification:** RFC 9639 (The Free Lossless Audio Codec Specification)  
**Date:** 2026-08-24  
**Status:** Approved  
**Author:** Google DeepMind / pair programming with Moolet  
**Target Environments:** x64/Linux, macOS, and 32-bit Microcontrollers (Teensy 4.x, NXP i.MX RT1176, ESP32)  
**License:** Clean-room MIT Re-write (Zero Copyleft / GPL code)

---

## 1. Overview & Goals

This specification defines a clean-room, portable, zero-dynamic-allocation C++17 library for encoding and decoding FLAC lossless audio streams compliant with IETF RFC 9639.

### Key Objectives
1. **Zero Runtime Dynamic Allocation (`no malloc / no new`)**: Predictable static memory footprint configurable via template parameters (`FlacDecoderBase<MaxChannels, MaxBlockSize>`).
2. **Dual Polymorphic and Bit-Exact Integer APIs**:
   - Implements abstract `AudioDecoder` and `AudioEncoder` base classes using normalized `float*` buffers (`[-1.0, 1.0]`).
   - Provides direct integer overloads (`int32_t*`, `int16_t*`) for bit-exact lossless streaming on embedded microcontrollers without floating-point conversion overhead.
3. **Full RFC 9639 Standard Compliance**:
   - Container format (`"fLaC"`, `STREAMINFO`, metadata block parsing/skipping).
   - CRC-8 header protection, CRC-16-FLAC frame footer checksum, and 128-bit unencoded PCM MD5 verification.
   - All 4 stereo decorrelation modes (Independent, Left-Side, Right-Side, Mid-Side).
   - All 4 subframe types: Constant, Verbatim, Fixed Predictor (orders 0..4), and Linear Predictor (LPC orders 1..32).
   - Partitioned Rice coding (4-bit & 5-bit parameters, escape partitions, zigzag folding).
4. **Configurable Encoder Compression (Levels 0–8)**:
   - Levels 0–2: Fast integer fixed predictors (orders 0–4) for real-time microcontroller recording.
   - Levels 3–8: Autocorrelation and Levinson-Durbin LPC analysis with selectable order (4..12) and partition searches for maximum compression.

---

## 2. Architecture & File Structure

```
audio-codecs/
├── include/audio_codecs/
│   ├── audio_codecs.h               # Umbrella include header
│   └── flac/
│       ├── flac_decoder.h           # Public FlacDecoder interface (dual float* & int32_t*)
│       └── flac_encoder.h           # Public FlacEncoder interface (dual float* & int32_t*)
├── src/flac/
│   ├── flac_common.h                # Frame headers, metadata structs, subframe enums
│   ├── crc.h / crc.cpp              # CRC-8 (0x07) and CRC-16-FLAC (0x8005) engines
│   ├── md5.h / md5.cpp              # RFC 1321 MD5 calculation engine
│   ├── metadata.h / metadata.cpp    # STREAMINFO builder/parser & metadata skipping
│   ├── decoder/
│   │   ├── rice_decoder.h / .cpp    # 4-bit/5-bit partitioned Rice reader & zigzag unfolding
│   │   ├── subframe_decoder.h / .cpp# Constant, Verbatim, Fixed (0..4), LPC (1..32) decoding
│   │   ├── channel_decorrelator.h / .cpp # Stereo undo: Left-Side, Right-Side, Mid-Side
│   │   └── flac_decoder.cpp         # Decoder orchestrator
│   └── encoder/
│       ├── fixed_predictor.h / .cpp # Fast order 0..4 residual estimation
│       ├── lpc_analyzer.h / .cpp    # Autocorrelation + Levinson-Durbin recursion
│       ├── rice_encoder.h / .cpp    # Optimal Rice parameter selection & bit writer
│       ├── channel_decorrelator.h / .cpp # Optimal stereo mode selector (L/R, L-Side, R-Side, M-Side)
│       └── flac_encoder.cpp         # Frame builder, bitstream packing, header generator
└── tests/
    ├── test_flac_crc.cpp            # CRC-8 and CRC-16-FLAC test vectors
    ├── test_flac_md5.cpp            # MD5 calculation test vectors
    ├── test_flac_rice.cpp           # Rice coding roundtrip tests
    ├── test_flac_predictors.cpp     # Fixed & LPC predictor reconstruction tests
    ├── test_flac_decorrelator.cpp   # Stereo decorrelation roundtrip tests
    ├── test_flac_decoder.cpp        # FlacDecoder facade test
    ├── test_flac_encoder.cpp        # FlacEncoder facade test
    ├── test_flac_roundtrip.cpp      # Bit-exact lossless roundtrip verification (SNR = inf, diff = 0)
    └── test_real_flacs.cpp          # Real-world FLAC file decoding from ybrid/test-files
```

---

## 3. Mathematical Specifications & RFC 9639 Formulations

### 3.1 Checksums & Hashes
- **CRC-8-FLAC**: Generator polynomial $P(x) = x^8 + x^2 + x^1 + 1$ (`0x07`), initialized with `0x00`. Computed over all bytes of the frame header up to (but not including) the 8-bit CRC byte.
- **CRC-16-FLAC**: Generator polynomial $P(x) = x^{16} + x^{15} + x^2 + 1$ (`0x8005`), initialized with `0x0000`. Computed over all bytes of the frame from the first sync byte through the end of the last subframe.
- **MD5 Signature**: 128-bit MD5 hash computed over raw unencoded little-endian signed audio samples.

### 3.2 Interchannel Decorrelation
FLAC frames define 4 channel assignments:
- **Independent Channels ($0 \dots 7$)**: Channels are coded independently without cross-channel prediction.
- **Left-Side ($8$)**: Channel 0 is Left ($L$), Channel 1 is Side ($S = L - R$). Reconstruction:
  $$\text{Right} = L - S$$
- **Right-Side ($9$)**: Channel 0 is Side ($S = L - R$), Channel 1 is Right ($R$). Reconstruction:
  $$\text{Left} = R + S$$
- **Mid-Side ($10$)**: Channel 0 is Mid ($M = \lfloor(L + R)/2\rfloor$), Channel 1 is Side ($S = L - R$). Exact integer reconstruction:
  $$\text{Left} = M + \left\lfloor \frac{S + (S \pmod 2)}{2} \right\rfloor = M + (S \gg 1) + (S \,\&\, 1)$$
  $$\text{Right} = \text{Left} - S = M - (S \gg 1)$$

### 3.3 Subframe Formats & Predictors

#### 1. Constant Subframe (Type `000000`)
- Followed by 1 unencoded sample stored in the subframe's bit depth. All $N$ samples in the subframe take this value.

#### 2. Verbatim Subframe (Type `000001`)
- Followed by $N$ unencoded samples stored sequentially in the subframe's bit depth.

#### 3. Fixed Predictor Subframe (Type `001000` to `001100`, Orders 0..4)
- Contains $k$ unencoded warm-up samples ($k = \text{order} \in \{0, 1, 2, 3, 4\}$).
- Predictor formulations:
  - **Order 0**: $\hat{s}(t) = 0$
  - **Order 1**: $\hat{s}(t) = s(t-1)$
  - **Order 2**: $\hat{s}(t) = 2s(t-1) - s(t-2)$
  - **Order 3**: $\hat{s}(t) = 3s(t-1) - 3s(t-2) + s(t-3)$
  - **Order 4**: $\hat{s}(t) = 4s(t-1) - 6s(t-2) + 4s(t-3) - s(t-4)$
- Residual: $r(t) = s(t) - \hat{s}(t)$. Decode reconstruction: $s(t) = r(t) + \hat{s}(t)$.

#### 4. Linear Predictor Subframe (Type `100000` to `111111`, Orders 1..32)
- Contains $l$ unencoded warm-up samples ($l = \text{order} \in [1, 32]$).
- 4 bits: `(coefficient_precision) - 1` (0b1111 forbidden).
- 5 bits: `prediction_right_shift` $q$ (signed 5-bit, non-negative).
- $l \times \text{precision}$ bits: Quantized signed integer coefficients $c_1 \dots c_l$.
- Prediction formulation:
  $$\hat{s}(t) = \left\lfloor \frac{\sum_{i=1}^l c_i \cdot s(t-i)}{2^q} \right\rfloor$$
- Decode reconstruction: $s(t) = r(t) + \hat{s}(t)$.

#### 5. Wasted Bits per Sample
- If subframe header indicates $w$ wasted bits, all samples in subframe were shifted right by $w$ bits ($s(t) \gg w$).
- After decoding prediction and residual, the samples are restored via $s(t) \ll w$.

### 3.4 Partitioned Rice Coding
- **Zigzag Encoding (Folding)**:
  $$u(t) = \begin{cases} 2 \cdot r(t) & \text{if } r(t) \ge 0 \\ -2 \cdot r(t) - 1 & \text{if } r(t) < 0 \end{cases}$$
- **Rice Code Representation**: For Rice parameter $k$:
  - Unary quotient: $Q = u(t) \gg k$, emitted as $Q$ zero bits followed by a '1' bit.
  - Binary remainder: $R = u(t) \,\&\, ((1 \ll k) - 1)$, emitted as $k$ raw bits.
- **Zigzag Decoding (Unfolding)**:
  $$r(t) = \begin{cases} u(t) \gg 1 & \text{if } u(t) \text{ is even} \\ -(u(t) \gg 1) - 1 & \text{if } u(t) \text{ is odd} \end{cases}$$
- **Escape Partition**: If Rice parameter is all 1s (`0b1111` for 4-bit or `0b11111` for 5-bit), the partition is escaped: 5 bits contain the bit depth $w$, followed by raw two's complement unencoded residual values.

---

## 4. Encoder Analysis & Optimization Algorithms

### 4.1 Autocorrelation & Levinson-Durbin LPC Analysis
For LPC compression (levels 3–8):
1. **Windowing**: Apply Hann or Tukey window to input signal $x[0 \dots N-1]$ to minimize boundary artifacts.
2. **Autocorrelation**:
   $$R[k] = \sum_{n=k}^{N-1} x_w[n] \cdot x_w[n-k], \quad k = 0 \dots l$$
3. **Levinson-Durbin Recursion**:
   Initialize $E_0 = R[0]$. For $i = 1 \dots l$:
   $$\kappa_i = -\frac{R[i] + \sum_{j=1}^{i-1} a_j^{(i-1)} R[i-j]}{E_{i-1}}$$
   $$a_i^{(i)} = \kappa_i$$
   $$a_j^{(i)} = a_j^{(i-1)} + \kappa_i \cdot a_{i-j}^{(i-1)}, \quad j = 1 \dots i-1$$
   $$E_i = E_{i-1} \cdot (1 - \kappa_i^2)$$
4. **Quantization & Shift Selection**: Scale float coefficients $a_i$ into signed integers with optimal right-shift $q \in [0, 31]$.

### 4.2 Optimal Rice Parameter Estimation
For a partition with $M$ residual samples and sum of folded values $S = \sum_{m=0}^{M-1} u_m$:
The expected optimal Rice parameter is:
$$k = \max\left(0, \left\lfloor \log_2\left( \frac{S}{M} \cdot \ln 2 \right) \right\rfloor\right)$$
Exhaustive search around $k \pm 1$ confirms the exact minimal bit count.

---

## 5. API Design & Memory Budget

### 5.1 Memory Layout
```cpp
template <size_t MaxChannels = 2, size_t MaxBlockSize = 4096>
class FlacDecoderBase : public AudioDecoder {
    alignas(16) uint8_t state_buffer_[65536]; // ~64 KB static pre-allocated buffer
    ...
};
```
- Sample buffer: $2 \times 4096 \times 4 = 32\,\text{KB}$
- Residual buffer: $2 \times 4096 \times 4 = 32\,\text{KB}$
- Zero dynamic allocations (`no malloc / no new`).

### 5.2 Public Header APIs
- `FlacDecoderBase::decode_frame(...)` (float $[-1.0, 1.0]$)
- `FlacDecoderBase::decode_frame_i32(...)` (bit-exact 32-bit integer PCM)
- `FlacDecoderBase::decode_frame_i16(...)` (bit-exact 16-bit integer PCM)
- `FlacEncoderBase::encode_frame(...)` (float $[-1.0, 1.0]$)
- `FlacEncoderBase::encode_frame_i32(...)` (bit-exact 32-bit integer PCM)
- `FlacEncoderBase::encode_frame_i16(...)` (bit-exact 16-bit integer PCM)
- Stream header readers/writers and metadata inspectors.

---

## 6. Verification Plan

1. **Unit Tests**:
   - `test_flac_crc`: Test standard CRC-8 and CRC-16-FLAC test vectors.
   - `test_flac_md5`: MD5 calculation on known byte strings.
   - `test_flac_rice`: Rice coding roundtrip over random and edge-case residuals.
   - `test_flac_predictors`: Fixed 0..4 and LPC order 1..12 reconstruction.
   - `test_flac_decorrelator`: All 4 stereo modes with positive and negative sample swings.
2. **Lossless Roundtrip Test**:
   - `test_flac_roundtrip`: Encode multi-frequency PCM $\rightarrow$ Decode FLAC $\rightarrow$ Verify $100\%$ sample equality ($\text{error} = 0$, $\text{SNR} = \infty$).
3. **Real-World Integration Test**:
   - `test_real_flacs`: Automatically fetch and decode real `.flac` test files from `https://github.com/ybrid/test-files` via CMake `FetchContent`.
