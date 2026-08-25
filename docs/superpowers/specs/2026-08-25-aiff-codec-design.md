# AIFF Audio Codec (Encoder & Decoder) Design Specification

**Specification:** Apple Audio Interchange File Format (AIFF) & AIFF-C (AIFC / SANE / RFC 1445)  
**Date:** 2026-08-25  
**Status:** Approved  
**Author:** Google DeepMind / pair programming with Moolet  
**Target Environments:** x64/Linux, macOS, and 32-bit Embedded Microcontrollers (Teensy 4.x, NXP i.MX RT1176, ESP32)  
**License:** Clean-room MIT Re-write (Zero Copyleft / GPL code)

---

## 1. Overview & Goals

This specification defines a clean-room, portable, zero-dynamic-allocation C++17 library for encoding and decoding standard Apple Audio Interchange File Format (`AIFF`) and Compressed Audio Interchange File Format (`AIFC` / `AIFF-C`) audio streams.

### Key Objectives
1. **Zero Runtime Dynamic Allocation (`no malloc / no new`)**:
   - Deterministic static memory footprint configurable via template parameters (`AiffDecoderBase<MaxChannels>`, `AiffEncoderBase<MaxChannels>`).
2. **Dual Polymorphic and Bit-Exact Integer APIs**:
   - Implements abstract `AudioDecoder` and `AudioEncoder` base classes using normalized `float*` buffers (`[-1.0, 1.0]`).
   - Provides direct integer overloads (`int16_t*`, `int32_t*`) and direct `float*` for zero-conversion-overhead streaming.
3. **Comprehensive Sample Format & Bit-Depth Support**:
   - **8-bit signed integer PCM** (`int8_t`, two's complement, range $[-128, 127]$, $0 = \text{silence}$)
   - **16-bit signed Big-Endian integer PCM** (`int16_t` BE)
   - **24-bit packed signed Big-Endian integer PCM** (3 bytes per sample)
   - **32-bit signed Big-Endian integer PCM** (`int32_t` BE)
   - **`sowt` Little-Endian PCM** (16-bit, 24-bit, 32-bit byte-swapped PCM in AIFC)
   - **32-bit IEEE 754 floating-point PCM** (`fl32` / `FL32` in AIFC)
   - **8-bit ITU-T G.711 A-law and $\mu$-law companded audio** (`alaw` / `ulaw` in AIFC)
4. **Standard Container & Chunk Compatibility**:
   - FORM container header parsing and generation (`'AIFF'` and `'AIFC'`).
   - Format Version Chunk (`'FVER'`, timestamp `0xA2805140`).
   - Common Chunk (`'COMM'`) with IEEE 754 80-bit SANE extended-precision float sample rate conversion and Pascal string (`pstring`) compression names.
   - Sound Data Chunk (`'SSND'`) with `offset` and `blockSize` support.
   - Safe streaming skip of auxiliary metadata chunks (`MARK`, `INST`, `COMT`, `ANNO`, `AUTH`, `(c) `, `NAME`, `APPL`, `ID3 `, `id3 `, `JUNK`, `PAD `).
   - **IFF 2-byte chunk alignment**: automatic pad byte tracking for odd-length chunks.
5. **Re-entrant Streaming Parser**:
   - Incrementally parses incoming chunk streams across arbitrary buffer boundaries without requiring full file pre-buffering.

---

## 2. Architecture & File Structure

```
audio-codecs/
├── include/audio_codecs/
│   ├── aiff.h                       # Public umbrella include
│   └── aiff/
│       ├── aiff_types.h             # Public structs, enums, format tags, FourCCs
│       ├── aiff_decoder.h           # AiffDecoderBase<MaxChannels> class interface
│       └── aiff_encoder.h           # AiffEncoderBase<MaxChannels> class interface
├── src/aiff/
│   ├── aiff_common.h                # IFF/AIFF FourCC constants, header structs, bit helpers
│   ├── ieee80.h / ieee80.cpp        # Portable IEEE 754 80-bit extended float conversions
│   ├── sample_converter.h / .cpp    # Signed 8-bit, 16/24/32 BE, sowt LE, Float32, G.711 converters
│   ├── decoder/
│   │   ├── aiff_parser.h / .cpp     # Re-entrant streaming IFF/AIFF chunk scanner
│   │   ├── aiff_decoder_impl.h      # Template implementation for AiffDecoderBase
│   │   └── aiff_decoder.cpp         # Non-template helper methods & explicit instantiations
│   └── encoder/
│       ├── aiff_encoder_impl.h      # Template implementation for AiffEncoderBase
│       └── aiff_encoder.cpp         # Non-template helper methods & explicit instantiations
└── tests/
    ├── test_aiff_ieee80.cpp         # 80-bit float roundtrip & standard vector tests
    ├── test_aiff_converters.cpp     # Bit-exact PCM (signed 8, 16/24/32 BE, sowt LE, f32) tests
    ├── test_aiff_parser.cpp         # Chunk parsing, fragmented streams & AIFC tests
    ├── test_aiff_decoder.cpp        # AiffDecoderBase decode tests
    ├── test_aiff_encoder.cpp        # AiffEncoderBase header writing & encoding tests
    └── test_aiff_roundtrip.cpp      # Lossless roundtrip tests across all formats
```

---

## 3. Data Structures & Constants

### 3.1 Format Enums & FourCCs (`aiff_types.h`)

```cpp
namespace audio_codecs::aiff {

enum class AiffFormType : uint32_t {
    Aiff = 0x41494646,  // 'AIFF'
    Aifc = 0x41494643   // 'AIFC'
};

enum class AiffCompressionType : uint32_t {
    None = 0x4E4F4E45,  // 'NONE' - Big-endian uncompressed PCM
    Sowt = 0x736F7774,  // 'sowt' - Little-endian uncompressed PCM ("twos" swapped)
    Fl32 = 0x666C3332,  // 'fl32' - 32-bit IEEE 754 floating point
    FL32 = 0x464C3332,  // 'FL32' - 32-bit IEEE float alternate
    ALaw = 0x616C6177,  // 'alaw' - 8-bit ITU-T G.711 A-law
    MuLaw= 0x756C6177,  // 'ulaw' - 8-bit ITU-T G.711 µ-law
    In24 = 0x696E3234,  // 'in24' - 24-bit integer
    In32 = 0x696E3332   // 'in32' - 32-bit integer
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

---

## 4. Detailed Component Specifications

### 4.1 IEEE 754 80-Bit Extended Precision Float Converter (`ieee80.h / .cpp`)

In the AIFF specification, sample rates are represented in the `COMM` chunk as 10-byte (80-bit) Big-Endian IEEE 754 / Apple SANE extended precision numbers:
- **Sign bit**: Bit 79 (MSB of byte 0).
- **Exponent**: Bits 78..64 (15 bits, unsigned bias $16383 = \text{0x3FFF}$).
- **Significand / Mantissa**: Bits 63..0 (64 bits, explicit leading integer bit at bit 63).

$$\text{Value } v = (-1)^s \cdot \frac{m}{2^{63}} \cdot 2^{e - 16383} = (-1)^s \cdot m \cdot 2^{e - 16446}$$

#### Portable Converter API
```cpp
void uint32_to_ieee80(uint32_t sample_rate, uint8_t out[10]);
void double_to_ieee80(double value, uint8_t out[10]);
uint32_t ieee80_to_uint32(const uint8_t in[10]);
double ieee80_to_double(const uint8_t in[10]);
```
- Performs bitwise shifts without depending on platform-specific `sizeof(long double)`.
- Verified against exact AIFF standard vectors (e.g. `44100` $\to$ `[0x40, 0x0E, 0xAC, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]`).

---

### 4.2 Sample Conversions & G.711 Companding

#### 8-bit Signed PCM (`int8_t`)
- Signed two's complement integer $s_8 \in [-128, 127]$, silence $= 0$.
- Normalized Float: $f = s_8 / 128.0\text{f}$
- Direct Int16: $i_{16} = s_8 \ll 8$
- Direct Int32: $i_{32} = s_8 \ll 24$

#### 16-bit Signed Big-Endian PCM (`int16_t` BE)
- 2 bytes $(b_0, b_1)$ unpacked as $s_{16} = (\text{int16\_t})((b_0 \ll 8) | b_1)$.
- Normalized Float: $f = s_{16} / 32768.0\text{f}$
- Direct Int32: $i_{32} = \text{int32\_t}(s_{16}) \ll 16$

#### 24-bit Packed Signed Big-Endian PCM (3 bytes/sample)
- 3 bytes $(b_0, b_1, b_2)$ unpacked with sign extension:
  $$s_{24} = ((\text{int32\_t}(b_0) \ll 24) \mid (\text{int32\_t}(b_1) \ll 16) \mid (\text{int32\_t}(b_2) \ll 8)) \gg 8$$
- Normalized Float: $f = s_{24} / 8388608.0\text{f}$
- Direct Int16: $i_{16} = \text{int16\_t}(s_{24} \gg 8)$
- Direct Int32: $i_{32} = s_{24} \ll 8$

#### 32-bit Signed Big-Endian PCM (`int32_t` BE)
- 4 bytes $(b_0, b_1, b_2, b_3)$ unpacked as $s_{32} = (\text{int32\_t})((b_0 \ll 24) \mid (b_1 \ll 16) \mid (b_2 \ll 8) \mid b_3)$.
- Normalized Float: $f = s_{32} / 2147483648.0\text{f}$
- Direct Int16: $i_{16} = \text{int16\_t}(s_{32} \gg 16)$

#### `sowt` Little-Endian PCM (16/24/32-bit)
- Unpacked with Little-Endian byte ordering and standard scale factors.

#### 32-bit IEEE 754 Float (`fl32` / `FL32`)
- Stored as Big-Endian IEEE 754 32-bit float; converted to/from integer buffers with saturation clamping.

#### ITU-T G.711 A-law and $\mu$-law (`alaw` / `ulaw`)
- Decoded via 256-entry static linear lookup tables.
- Encoded via standard 8-segment logarithmic companding curves.

---

### 4.3 Streaming IFF/AIFF Parser (`AiffParser`)

```cpp
enum class AiffParserState {
    SearchForm,       // 12 bytes: "FORM" <form_size> "AIFF" / "AIFC"
    ReadChunkHeader,  // 8 bytes: <chunk_id> <chunk_size>
    ReadCommChunk,    // COMM payload (18 bytes standard, >=22 bytes AIFC)
    ReadFverChunk,    // FVER payload (4 bytes timestamp 0xA2805140)
    ReadSsndHeader,   // SSND header (4 bytes offset + 4 bytes blockSize)
    SkipSsndOffset,   // Skipping non-zero SSND offset bytes (if offset > 0)
    SkipChunkPayload, // Skipping auxiliary metadata chunks (with even-byte alignment)
    StreamData,       // Ready to stream audio samples from SSND chunk
    Error             // Malformed header or corrupted stream
};
```

- Operates in a fixed 64-byte internal scratch buffer.
- Maintains `bytes_remaining_in_chunk` and tracks odd-byte padding ($1 \text{ pad byte if } \text{chunk\_size} \pmod 2 \neq 0$).
- Sets stream metadata: `sample_rate`, `channels`, `bits_per_sample`, `form_type`, `compression_type`, `sample_format`, `total_frames`, `ssnd_offset`, `ssnd_block_size`.

---

### 4.4 Decoder Class (`AiffDecoderBase<MaxChannels>`)

```cpp
template <size_t MaxChannels = 2>
class AiffDecoderBase : public AudioDecoder {
public:
    AiffDecoderBase();
    ~AiffDecoderBase() override;

    bool init(const AudioConfig& config) override;
    void reset() override;

    // Polymorphic float decode [-1.0f, 1.0f]
    int decode_frame(const uint8_t* in_data, size_t in_bytes, 
                     float* out_pcm, size_t max_out_samples) override;

    // Direct integer decodes
    int decode_frame_i16(const uint8_t* in_data, size_t in_bytes, 
                         int16_t* out_pcm, size_t max_out_samples);
    int decode_frame_i32(const uint8_t* in_data, size_t in_bytes, 
                         int32_t* out_pcm, size_t max_out_samples);
    int decode_frame_f32(const uint8_t* in_data, size_t in_bytes, 
                         float* out_pcm, size_t max_out_samples);

    // Stream container parser
    bool parse_stream_header(const uint8_t* in_data, size_t in_bytes, size_t& bytes_consumed);

    // Metadata accessors
    uint32_t            get_sample_rate() const;
    uint8_t             get_channels() const;
    uint8_t             get_bit_depth() const;
    AiffFormType        get_form_type() const;
    AiffCompressionType get_compression_type() const;
    AiffSampleFormat    get_sample_format() const;
    uint64_t            get_total_frames() const;
    size_t              get_last_frame_bytes() const;
};

using AiffDecoder = AiffDecoderBase<2>;
```

---

### 4.5 Encoder Class (`AiffEncoderBase<MaxChannels>`)

```cpp
template <size_t MaxChannels = 2>
class AiffEncoderBase : public AudioEncoder {
public:
    AiffEncoderBase();
    ~AiffEncoderBase() override;

    bool init(const AudioConfig& config) override;
    bool init_aiff(const AiffEncoderConfig& config);
    void reset() override;

    // Polymorphic float encode [-1.0f, 1.0f]
    int encode_frame(const float* in_pcm, size_t in_samples, 
                     uint8_t* out_data, size_t max_out_bytes) override;

    // Direct integer encodes
    int encode_frame_i16(const int16_t* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);
    int encode_frame_i32(const int32_t* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);
    int encode_frame_f32(const float* in_pcm, size_t in_samples, 
                         uint8_t* out_data, size_t max_out_bytes);

    // Header generation & in-place finalization
    int write_stream_header(uint8_t* out_data, size_t max_out_bytes);
    int finalize_header(uint8_t* header_ptr, uint32_t total_data_bytes);
    int flush(uint8_t* out_data, size_t max_out_bytes) override;

    // Accessors
    uint32_t            get_sample_rate() const;
    uint8_t             get_channels() const;
    uint8_t             get_bit_depth() const;
    uint64_t            get_total_frames() const;
    AiffFormType        get_form_type() const;
    AiffCompressionType get_compression_type() const;
};

using AiffEncoder = AiffEncoderBase<2>;
```

---

## 5. Verification & Test Plan

1. **`test_aiff_ieee80`**:
   - Exhaustive tests for `uint32_to_ieee80`, `double_to_ieee80`, `ieee80_to_uint32`, `ieee80_to_double`.
   - Checks against standard AIFF test vectors (8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 384000) and custom non-standard sample rates.
2. **`test_aiff_converters`**:
   - Unit tests for conversions between raw formats and `float*`, `int16_t*`, `int32_t*`.
   - Checks signed 8-bit PCM (two's complement, 0 = silence), 16/24/32-bit Big-Endian PCM, `sowt` Little-Endian, `fl32`, and G.711 `alaw`/`ulaw`.
3. **`test_aiff_parser`**:
   - Incrementally feed header bytes in 1, 3, and 7 byte fragments.
   - Out-of-order and auxiliary metadata chunks (`MARK`, `INST`, `COMT`, `ANNO`, `ID3 `, `id3 `).
   - IFF odd-length chunk padding validation.
   - `SSND` chunks with non-zero `offset` and `blockSize`.
4. **`test_aiff_decoder`**:
   - Decode generated AIFF and AIFC streams into normalized float, int16, and int32 buffers.
5. **`test_aiff_encoder`**:
   - Write and finalize standard `AIFF` and `AIFC` headers.
6. **`test_aiff_roundtrip`**:
   - Full encode $\rightarrow$ decode roundtrip verification for:
     - 8-bit signed PCM (Lossless)
     - 16-bit signed BE PCM (Lossless: $\text{diff} = 0$, $\text{SNR} = \infty$)
     - 24-bit packed signed BE PCM (Lossless: $\text{diff} = 0$, $\text{SNR} = \infty$)
     - 32-bit signed BE PCM (Lossless: $\text{diff} = 0$, $\text{SNR} = \infty$)
     - `sowt` 16/24/32-bit LE PCM (Lossless)
     - 32-bit IEEE Float (Lossless exact match)
     - 8-bit G.711 A-law / $\mu$-law
     - Multi-channel layouts (Mono, Stereo, Multichannel).
