# WAV Audio Codec (Encoder & Decoder) Design Specification

**Specification:** Microsoft RIFF / WAVE Audio Format & ITU-T G.711  
**Date:** 2026-08-25  
**Status:** Approved  
**Author:** Google DeepMind / pair programming with Moolet  
**Target Environments:** x64/Linux, macOS, and 32-bit Embedded Microcontrollers (Teensy 4.x, NXP i.MX RT1176, ESP32)  
**License:** Clean-room MIT Re-write (Zero Copyleft / GPL code)

---

## 1. Overview & Goals

This specification defines a clean-room, portable, zero-dynamic-allocation C++17 library for encoding and decoding standard RIFF/WAVE `.wav` audio streams.

### Key Objectives
1. **Zero Runtime Dynamic Allocation (`no malloc / no new`)**:
   - Predictable static memory footprint configurable via template parameters (`WavDecoderBase<MaxChannels>`, `WavEncoderBase<MaxChannels>`).
2. **Dual Polymorphic and Bit-Exact Integer APIs**:
   - Implements abstract `AudioDecoder` and `AudioEncoder` base classes using normalized `float*` buffers (`[-1.0, 1.0]`).
   - Provides direct integer overloads (`int16_t*`, `int32_t*`) and `float*` for bit-exact streaming without floating-point conversion overhead.
3. **Comprehensive Sample Format & Bit-Depth Support**:
   - 8-bit unsigned integer PCM (`uint8_t`, $128 = \text{silence}$)
   - 16-bit signed Little-Endian integer PCM (`int16_t`)
   - 24-bit packed signed Little-Endian integer PCM (3 bytes per sample)
   - 32-bit signed Little-Endian integer PCM (`int32_t`)
   - 32-bit IEEE 754 floating-point PCM
   - 8-bit ITU-T G.711 A-law and $\mu$-law companded formats
4. **Standard Container & Extension Compatibility**:
   - RIFF and WAVE header parsing/generation.
   - `WAVE_FORMAT_EXTENSIBLE` handling (speaker channel masks and sub-format GUIDs).
   - `fact` chunk generation/parsing for non-PCM and floating-point formats.
   - Safe streaming skip of metadata chunks (`bext`, `LIST`/`INFO`, `id3 `, `JUNK`, `PAD `) with odd-byte alignment padding support.
5. **Re-entrant Streaming Parser**:
   - Incrementally parses incoming chunk streams across arbitrary buffer boundaries without requiring full file pre-buffering.

---

## 2. Architecture & File Structure

```
audio-codecs/
├── include/audio_codecs/
│   ├── wav.h                        # Public umbrella include
│   └── wav/
│       ├── wav_types.h              # Public structs, enums, format tags, speaker masks
│       ├── wav_decoder.h            # WavDecoderBase<MaxChannels> class interface
│       └── wav_encoder.h            # WavEncoderBase<MaxChannels> class interface
├── src/wav/
│   ├── wav_common.h                 # RIFF/WAVE 4CC constants and header structs
│   ├── g711.h / g711.cpp            # Fast 256-entry LUTs for A-law / µ-law conversions
│   ├── sample_converter.h / .cpp    # 8-bit, 16-bit, 24-bit, 32-bit PCM & Float converters
│   ├── decoder/
│   │   ├── wav_parser.h / .cpp      # Re-entrant streaming RIFF chunk scanner
│   │   ├── wav_decoder_impl.h       # Template implementation for WavDecoderBase
│   │   └── wav_decoder.cpp          # Non-template helper implementations
│   └── encoder/
│       ├── wav_encoder_impl.h       # Template implementation for WavEncoderBase
│       └── wav_encoder.cpp          # Non-template helper implementations
└── tests/
    ├── test_wav_g711.cpp            # ITU-T G.711 A-law / µ-law roundtrip test vectors
    ├── test_wav_converters.cpp      # Bit-exact PCM (8, 16, 24, 32, float) conversion tests
    ├── test_wav_parser.cpp          # Chunk parsing, fragmented stream & extensible header tests
    ├── test_wav_decoder.cpp         # WavDecoderBase decode tests
    ├── test_wav_encoder.cpp         # WavEncoderBase header writing & encoding tests
    └── test_wav_roundtrip.cpp       # Lossless roundtrip tests across all bit depths
```

---

## 3. Data Structures & Constants

### 3.1 Format Tags & GUIDs (`wav_types.h` & `wav_common.h`)

```cpp
namespace audio_codecs::wav {

enum class WavFormat : uint16_t {
    Pcm        = 0x0001,
    IeeeFloat  = 0x0003,
    ALaw       = 0x0006,
    MuLaw      = 0x0007,
    Extensible = 0xFFFE
};

enum class WavSampleFormat {
    Uint8,      // 8-bit unsigned integer
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

// SubFormat GUID for KSDATAFORMAT_SUBTYPE_PCM:
// {00000001-0000-0010-8000-00aa00389b71}
constexpr uint8_t kGuidPcm[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

// SubFormat GUID for KSDATAFORMAT_SUBTYPE_IEEE_FLOAT:
// {00000003-0000-0010-8000-00aa00389b71}
constexpr uint8_t kGuidIeeeFloat[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

} // namespace audio_codecs::wav
```

---

## 4. Detailed Component Specifications

### 4.1 Sample Conversions & G.711 Companding

#### 8-bit Unsigned PCM
- Value $u \in [0, 255]$, silence $= 128$.
- Normalized Float: $f = (u - 128) / 128.0f$
- Direct Int16: $i_{16} = (u - 128) \ll 8$
- Direct Int32: $i_{32} = (u - 128) \ll 24$

#### 16-bit Signed Little-Endian PCM
- Value $s_{16} \in [-32768, 32767]$.
- Normalized Float: $f = s_{16} / 32768.0f$
- Direct Int32: $i_{32} = \text{int32\_t}(s_{16}) \ll 16$

#### 24-bit Packed Signed Little-Endian PCM
- 3 bytes $(b_0, b_1, b_2)$.
- Unpacked signed 24-bit integer: $s_{24} = ((\text{int32\_t}(b_0) \mid (\text{int32\_t}(b_1) \ll 8) \mid (\text{int32\_t}(b_2) \ll 16)) \ll 8) \gg 8$
- Normalized Float: $f = s_{24} / 8388608.0f$
- Direct Int16: $i_{16} = \text{int16\_t}(s_{24} \gg 8)$
- Direct Int32: $i_{32} = s_{24} \ll 8$

#### 32-bit Signed Little-Endian PCM
- Value $s_{32} \in [-2147483648, 2147483647]$.
- Normalized Float: $f = s_{32} / 2147483648.0f$
- Direct Int16: $i_{16} = \text{int16\_t}(s_{32} \gg 16)$

#### 32-bit IEEE 754 Float
- Value $f_{32} \in [-1.0f, +1.0f]$.
- Conversion to Int16: $\text{clamp}(\lfloor f_{32} \cdot 32767.5f \rfloor, -32768, 32767)$
- Conversion to Int32: $\text{clamp}(\lfloor f_{32} \cdot 2147483647.5 \rfloor, -2147483648LL, 2147483647LL)$

#### ITU-T G.711 A-law and $\mu$-law
- Table-driven decoding via 256-entry static lookup tables:
  - `kALawToLinear16[256]`
  - `kMuLawToLinear16[256]`
- Encoding via bitwise logarithmic segment mapping:
  - A-law: 8 segments, even bits inverted (`^ 0x55`).
  - $\mu$-law: Bias $+33$, 8 segments, full inversion (`~`).

---

### 4.2 Streaming RIFF Parser (`WavParser`)

```cpp
enum class ParserState {
    SearchRiff,       // Expecting 12-byte "RIFF" <size> "WAVE"
    ReadChunkHeader,  // Expecting 8-byte <chunk_id> <chunk_size>
    ReadFmtChunk,     // Reading fmt chunk payload
    ReadFactChunk,    // Reading fact chunk payload
    SkipChunkPayload, // Skipping auxiliary chunk data (bext, LIST, id3, etc.)
    StreamData,       // Stream reached 'data' chunk
    Error             // Encountered unrecoverable corruption
};
```

- Operates in fixed 64-byte internal scratch buffer.
- Maintains `bytes_remaining_in_chunk` and tracks odd-byte padding ($1 \text{ pad byte if } \text{chunk\_size} \pmod 2 \neq 0$).
- Sets stream metadata: `sample_rate`, `channels`, `bits_per_sample`, `format_tag`, `subformat_guid`, `channel_mask`, `data_chunk_size`.

---

### 4.3 Decoder Class (`WavDecoderBase<MaxChannels>`)

```cpp
template <size_t MaxChannels = 2>
class WavDecoderBase : public AudioDecoder {
public:
    WavDecoderBase();
    ~WavDecoderBase() override;

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
    uint32_t get_sample_rate() const;
    uint8_t  get_channels() const;
    uint8_t  get_bit_depth() const;
    WavFormat get_format_tag() const;
    uint32_t get_channel_mask() const;
    uint64_t get_total_samples() const;
    size_t   get_last_frame_bytes() const;
};

using WavDecoder = WavDecoderBase<2>;
```

---

### 4.4 Encoder Class (`WavEncoderBase<MaxChannels>`)

```cpp
template <size_t MaxChannels = 2>
class WavEncoderBase : public AudioEncoder {
public:
    WavEncoderBase();
    ~WavEncoderBase() override;

    bool init(const AudioConfig& config) override;
    bool init_wav(const WavEncoderConfig& config);
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
    uint32_t get_sample_rate() const;
    uint8_t  get_channels() const;
    uint8_t  get_bit_depth() const;
    uint64_t get_total_samples() const;
};

using WavEncoder = WavEncoderBase<2>;
```

---

## 5. Verification & Test Plan

1. **`test_wav_g711`**:
   - Exhaustive tests for all 256 A-law and 256 $\mu$-law values against ITU-T test tables.
2. **`test_wav_converters`**:
   - Unit tests for conversions between raw formats and `float*`, `int16_t*`, `int32_t*`.
   - Checks minimums, maximums, zero/silence, DC bias, sign extension, and float clamping.
3. **`test_wav_parser`**:
   - Incrementally feed header bytes in $1$, $3$, and $7$ byte fragments.
   - Out-of-order chunks (`LIST`, `bext`, `id3 ` before/after `fmt ` and `data`).
   - Extensible headers with speaker masks.
4. **`test_wav_decoder`**:
   - Decode generated WAV streams into normalized float, int16, and int32 buffers.
5. **`test_wav_encoder`**:
   - Write and finalize standard, extensible, and float headers with `fact` chunks.
6. **`test_wav_roundtrip`**:
   - Full encode $\rightarrow$ decode roundtrip verification for:
     - 8-bit unsigned PCM
     - 16-bit signed PCM (Lossless: $\text{diff} = 0$, $\text{SNR} = \infty$)
     - 24-bit packed PCM (Lossless: $\text{diff} = 0$, $\text{SNR} = \infty$)
     - 32-bit signed PCM (Lossless: $\text{diff} = 0$, $\text{SNR} = \infty$)
     - 32-bit IEEE Float (Lossless exact match)
     - 8-bit G.711 A-law / $\mu$-law
     - Multi-channel layouts (Mono, Stereo, 5.1 Surround).
