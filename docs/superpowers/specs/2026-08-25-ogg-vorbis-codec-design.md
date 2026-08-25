# Ogg/Vorbis Audio Codec (Encoder & Decoder) Design Specification

**Specification:** Vorbis I Specification (https://xiph.org/vorbis/doc/Vorbis_I_spec.html) & RFC 3533 (Ogg Encapsulation)  
**Date:** 2026-08-25  
**Status:** Approved  
**Author:** Google DeepMind / pair programming with Moolet  
**Target Environments:** x64/Linux, macOS, and 32-bit Microcontrollers (Teensy 4.x, NXP i.MX RT1176, ESP32)  
**License:** Clean-room MIT Re-write (Zero Copyleft / GPL code)

---

## 1. Overview & Goals

This specification defines a clean-room, zero-dynamic-allocation C++17 library for encoding and decoding Ogg bitstream containers and Vorbis I audio streams compliant with the Xiph.org Vorbis I specification and IETF RFC 3533.

### Key Objectives
1. **Zero Runtime Dynamic Allocation (`no malloc / no new`)**:
   - Predictable static memory footprint configurable via template parameters (`VorbisDecoderBase<MaxChannels, MaxBlockSize>`, `VorbisEncoderBase<MaxChannels, MaxBlockSize>`).
   - Zero heap allocations during steady-state frame encode/decode.
2. **Polymorphic Audio Interfaces**:
   - Implements abstract `AudioDecoder` and `AudioEncoder` base classes using normalized `float*` buffers (`[-1.0, 1.0]`).
   - Full `.ogg` container stream encapsulation and packet-level access APIs.
3. **Full Vorbis I Compliance**:
   - Header packet parsing & generation (Identification `0x01`, Comment `0x03`, Setup `0x05`).
   - Complete Huffman codebook engine (ordered/unordered codebooks, multidimensional vector quantization lookup types 0, 1, 2).
   - Floor Type 1 (piecewise linear Bark-scale interpolation and inverse-dB synthesis) and Floor Type 0.
   - Residue Types 0, 1, and 2 (interleaved and non-interleaved spectral vectors).
   - Mapping Type 0 with polar stereo channel coupling (magnitude & angle pairs).
   - Dynamic block size switching ($N_0$ short, $N_1$ long) with sine-of-sine windowing and overlap-add reconstruction.
   - $O(N \log N)$ fast forward MDCT and inverse IMDCT.
4. **RFC 3533 Ogg Bitstream Framing**:
   - Complete streaming demuxer/muxer with 27-byte page header parsing, segment table lacing, multi-page packet spanning, 32-bit CRC calculation (`0x04C11DB7`), and granule position timekeeping.

---

## 2. Architecture & File Structure

```
audio-codecs/
├── include/audio_codecs/
│   ├── audio_codecs.h               # Umbrella include header
│   ├── ogg/
│   │   ├── ogg_page.h               # Ogg page structures, header flags, lacing values
│   │   ├── ogg_demuxer.h            # Streaming page parser, packet assembler, CRC-32
│   │   └── ogg_muxer.h              # Packet segmenter, page builder, CRC-32 generator
│   └── vorbis/
│       ├── vorbis_decoder.h         # Public VorbisDecoder interface (implements AudioDecoder)
│       ├── vorbis_encoder.h         # Public VorbisEncoder interface (implements AudioEncoder)
│       └── vorbis_types.h           # Vorbis header structs, mode/mapping configurations
├── src/
│   ├── ogg/
│   │   ├── ogg_crc.h / .cpp         # Ogg 32-bit CRC (polynomial 0x04c11db7)
│   │   ├── ogg_demuxer.cpp          # Zero-allocation streaming demuxer & page sync
│   │   └── ogg_muxer.cpp            # Zero-allocation streaming muxer & packet segmenter
│   └── vorbis/
│       ├── vorbis_common.h / .cpp   # Vorbis constants, 32-bit float unpacker, window tables
│       ├── vorbis_mdct.h / .cpp     # Fast O(N log N) MDCT & IMDCT transforms
│       ├── vorbis_codebook.h / .cpp # Huffman codebook unpacker, tree builder, vector lookup
│       ├── vorbis_floor.h / .cpp    # Floor 1 (piecewise linear curve synthesis) & Floor 0
│       ├── vorbis_residue.h / .cpp  # Residue 0, 1, 2 vector decoding & encoding
│       ├── vorbis_mapping.h / .cpp  # Mapping 0, submaps, angle/magnitude channel coupling
│       ├── decoder/
│       │   ├── header_parser.h/.cpp # ID (0x01), Comment (0x03), Setup (0x05) packet parsers
│       │   ├── packet_decoder.h/.cpp# Per-packet floor/residue/coupling/IMDCT synthesis
│       │   ├── vorbis_decoder_impl.h# Template implementation
│       │   └── vorbis_decoder.cpp   # Facade orchestrator & overlap-add buffer manager
│       └── encoder/
│           ├── setup_builder.h/.cpp # Standard Setup header & codebook generator
│           ├── psychoacoustic.h/.cpp# Bark scale envelope calculation & Floor 1 fitting
│           ├── residue_quantizer.h/.cpp # Residue vector quantizer & codebook matching
│           ├── channel_coupling.h/.cpp  # Mid/side & angle/magnitude stereo coupling
│           ├── vorbis_encoder_impl.h# Template implementation
│           └── vorbis_encoder.cpp   # Frame orchestrator, window switcher & bitstream packer
└── tests/
    ├── test_ogg_crc.cpp             # Ogg CRC-32 test vectors
    ├── test_ogg_framing.cpp         # Page demuxing, packet lacing, multi-page spans
    ├── test_vorbis_float.cpp        # 32-bit Vorbis float bit-exact unpacking
    ├── test_vorbis_codebook.cpp     # Codebook unpacking, Huffman tree, vector lookup
    ├── test_vorbis_floor.cpp        # Floor 1 curve generation & interpolation
    ├── test_vorbis_mdct.cpp         # MDCT/IMDCT invertibility and windowing
    ├── test_vorbis_headers.cpp      # Header packet parsing (ID, Comment, Setup)
    ├── test_vorbis_decoder.cpp      # Audio packet decoding & overlap-add
    ├── test_vorbis_encoder.cpp      # Audio packet encoding & setup generation
    ├── test_vorbis_roundtrip.cpp    # Full encode -> decode audio verification
    └── test_real_vorbis.cpp         # Real-world .ogg decoding tests from test corpus
```

---

## 3. Mathematical Specifications & Vorbis I Algorithms

### 3.1 Ogg Page CRC-32 Checksum
- **Generator Polynomial**: $P(x) = x^{32} + x^{26} + x^{23} + x^{22} + x^{16} + x^{12} + x^{11} + x^{10} + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1$ (`0x04C11DB7`).
- **Calculation**: Standard 32-bit table lookup initialized to `0x00000000`, computed over the entire 27-byte Ogg header (with CRC field zeroed) plus page segment tables and payload bytes.

### 3.2 Vorbis 32-bit Floating-Point Unpacker
Vorbis codebook quantization parameters (`min`, `delta`) are stored as custom 32-bit floats:
$$\text{mantissa} = \text{val} \ \& \ \text{0x1FFFFF}, \quad \text{sign} = \text{val} \ \& \ \text{0x80000000}, \quad \text{exponent} = (\text{val} \ \& \ \text{0x7FE00000}) \gg 21$$
$$x = (\text{sign} \ ? \ -\text{mantissa} : \text{mantissa}) \cdot 2^{(\text{exponent} - 788)}$$

### 3.3 Vorbis Window Function & Block Switching
Vorbis uses a unique sine-of-sine window function:
$$w(n) = \sin\left(\frac{\pi}{2} \sin^2\left(\frac{\pi}{2N}\left(n + 0.5\right)\right)\right), \quad 0 \le n < N$$
When switching between short ($N_{\text{short}}$) and long ($N_{\text{long}}$) blocks:
- **Left Window Half**: Depends on `previous_window_flag` ($N_{\text{prev}}$). If 0, uses a short half-window padded with zeros/ones; if 1, uses a standard long half-window.
- **Right Window Half**: Depends on `next_window_flag` ($N_{\text{next}}$). If 0, uses a short half-window; if 1, uses a standard long half-window.

### 3.4 Fast $O(N \log N)$ MDCT / IMDCT
- **Forward MDCT** ($N$ audio samples $\to N/2$ spectral bins):
  $$X_k = \sum_{n=0}^{N-1} x_n \cos\left(\frac{\pi}{N}\left(n + \frac{1}{2} + \frac{N}{4}\right)\left(k + \frac{1}{2}\right)\right), \quad 0 \le k < N/2$$
- **Inverse IMDCT** ($N/2$ spectral bins $\to N$ time samples):
  $$y_n = \frac{2}{N} \sum_{k=0}^{N/2-1} X_k \cos\left(\frac{\pi}{N}\left(n + \frac{1}{2} + \frac{N}{4}\right)\left(k + \frac{1}{2}\right)\right), \quad 0 \le n < N$$
- Implemented in $O(N \log N)$ time using pre/post-twiddle complex multiplications with an $N/4$-point complex FFT (reusing `src/core/fft.h`).

### 3.5 Floor 1 Synthesis & Render Curve
- Floor 1 specifies spectral envelope curves via piecewise linear interpolation over non-linearly spaced $X$ coordinates (Bark frequency scale).
- Each $Y$ point is decoded using neighbor-based prediction:
  $$\text{predicted\_y} = \text{render\_point}(x_0, y_0, x_1, y_1, x) = y_0 + \left\lfloor (y_1 - y_0) \cdot \frac{x - x_0}{x_1 - x_0} \right\rfloor$$
- The interpolated integer $Y$ curve ($0 \dots 255$) is mapped to linear amplitude scale using the Vorbis spec inverse-dB table:
  $$\text{amplitude}(n) = \text{floor1\_inverse\_dB\_table}[Y_n] = 2^{(Y_n \cdot \text{scale} - \text{offset})}$$

### 3.6 Residue & Channel Coupling
- **Residue Types**:
  - **Type 0**: Interleaved by vector dimensions.
  - **Type 1**: Non-interleaved spectral bins per channel.
  - **Type 2**: Interleaved across channels (Channel 0 and Channel 1 coefficients grouped together into joint vector codebooks).
- **Polar Stereo Decoupling (Magnitude $M$, Angle $A$)**:
  $$\begin{cases}
  M > 0, A > 0 \implies L = M, \ R = M - A \\
  M > 0, A \le 0 \implies R = M, \ L = M + A \\
  M \le 0, A > 0 \implies L = M, \ R = M + A \\
  M \le 0, A \le 0 \implies R = M, \ L = M - A
  \end{cases}$$

---

## 4. Decoder Pipeline

1. **Header Parsing**:
   - `0x01` Identification packet $\to$ channels, sample rate, bitrates, block sizes $N_0, N_1$.
   - `0x03` Comment packet $\to$ vendor string, user metadata comments.
   - `0x05` Setup packet $\to$ Codebooks, Floors, Residues, Mappings, Modes.
2. **Audio Packet Decoding**:
   - Read mode number $\to$ retrieve block size ($N_0$ or $N_1$) and mapping index.
   - Read window flags if long block.
   - Decode spectral floor curves for active channels (Floor 1).
   - Decode residue vectors using assigned codebooks (Residue 0/1/2).
   - Undo polar channel coupling on magnitude/angle pairs.
   - Multiply residue spectra by floor curves ($X_c[k] = r_c[k] \cdot f_c[k]$).
   - Perform $N$-point IMDCT.
   - Apply sine-of-sine window.
   - Overlap-add with saved history buffer, emit valid PCM samples, and update overlap history.

---

## 5. Encoder Pipeline

1. **Setup & Initialization**:
   - Build standard Vorbis Setup header with optimized Bark-scale Floor 1 partitions, Residue 2 codebooks, stereo mapping with polar coupling, and dual-mode configuration (short/long blocks).
   - Write Ogg encapsulation headers with BOS flag.
2. **Audio Frame Encoding**:
   - Transient / attack detection across 4 sub-blocks $\to$ select short ($N_0$) or long ($N_1$) block.
   - Apply sine-of-sine windowing with appropriate transition slopes.
   - Compute forward MDCT $\to$ $N/2$ spectral bins.
   - Estimate Bark-scale energy envelope and fit Floor 1 piecewise linear points.
   - Apply polar stereo channel coupling for bitrate savings.
   - Divide spectral bins by floor envelope to get normalized residue.
   - Quantize residue into multi-stage vectors and encode using Huffman codebooks.
   - Pack mode, window flags, floor indices, and residue codewords into Vorbis bitstream.
   - Encapsulate packets into Ogg pages with CRC-32 and granule position timekeeping.

---

## 6. Memory Model & Zero-Allocation Invariants

- Pre-allocated template storage (`VorbisDecoderBase<MaxChannels, MaxBlockSize>`).
- Static setup tables for up to 64 codebooks, 4 floors, 4 residues, 4 mappings, 64 modes.
- Frame decode and frame encode execute in steady state with **zero heap allocations (`no malloc / no new`)**.
- Strict bounds checking on bitstream reads and writes.

---

## 7. Verification Plan

1. **Unit Tests**:
   - `test_ogg_crc`: RFC 3533 CRC-32 test vectors.
   - `test_ogg_framing`: Page parsing, segment table lacing, multi-page spans, granule pos.
   - `test_vorbis_float`: Bit-exact float unpacking test cases.
   - `test_vorbis_codebook`: Huffman prefix codes, tree traversal, vector lookups.
   - `test_vorbis_floor`: Floor 1 curve synthesis, point interpolation, neighbor prediction.
   - `test_vorbis_mdct`: MDCT/IMDCT invertibility, energy conservation, sine-of-sine windowing.
   - `test_vorbis_headers`: ID, Comment, Setup packet generation and parsing.
   - `test_vorbis_decoder` & `test_vorbis_encoder`: Component-level pipeline execution.
2. **Roundtrip Tests**:
   - `test_vorbis_roundtrip`: End-to-end PCM $\to$ Encode $\to$ Decode $\to$ PCM verification (SNR > 30 dB, transient response).
3. **Integration Tests**:
   - `test_real_vorbis`: Decoding real-world `.ogg` Vorbis files from test corpus.
