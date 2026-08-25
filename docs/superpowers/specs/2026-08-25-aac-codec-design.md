# Design Document: AAC-LC Codec & M4A Container Subsystem (`audio_codecs_aac` & `audio_codecs_mp4`)

## 1. Executive Summary
This document specifies the architecture, math, data structures, and implementation plan for a clean-room, MIT-licensed **AAC-LC (Low Complexity) Audio Codec** and **ISOBMFF (M4A) Container** subsystem for the `audio-codecs` C++17 library.

The subsystem is designed for high-performance desktop applications as well as resource-constrained 32-bit microcontrollers featuring single-precision FPUs and Tightly Coupled Memory (TCM), specifically targeting the **ARM Cortex-M7** family (e.g., Teensy 4.1 / NXP i.MX RT1062 @ 600 MHz and NXP i.MX RT1176 @ 1 GHz).

---

## 2. Standards Compliance & Scope
* **Decoder**: Normative compliance with **ISO/IEC 13818-7** (MPEG-2 AAC) and **ISO/IEC 14496-3 Subpart 4** (MPEG-4 AAC-LC).
* **Encoder**: Custom psychoacoustic model and rate controller producing fully compliant AAC-LC bitstreams.
* **Containers**:
  * **ADTS (Audio Data Transport Stream)**: Frame-by-frame streaming with optional 16-bit CRC.
  * **ISOBMFF / M4A (`.m4a` / `.mp4`)**: Stream-oriented chunk/sample demuxer and muxer parsing `ftyp`, `moov`, `trak`, `mdia`, `minf`, `stbl`, `esds` (`AudioSpecificConfig`), and `mdat` atoms.
* **Audio Formats Supported**:
  * Sample Rates: 8000 Hz, 11025 Hz, 12000 Hz, 16000 Hz, 22050 Hz, 24000 Hz, 32000 Hz, 44100 Hz, 48000 Hz, 64000 Hz, 88200 Hz, 96000 Hz.
  * Channels: Mono (1.0 - `ID_SCE`) and Stereo (2.0 - `ID_CPE` / M/S & Intensity Stereo).
  * Bitrates: 32 kbps to 320 kbps (CBR & VBR).

---

## 3. Directory Layout & Repository Integration

The new subsystem is partitioned into two modular CMake libraries:

```
audio-codecs/
├── include/audio_codecs/
│   ├── aac/
│   │   ├── aac_decoder.h           # Implements AudioDecoder interface
│   │   ├── aac_encoder.h           # Implements AudioEncoder interface
│   │   ├── aac_types.h             # Bitstream elements, frame header structs
│   │   └── adts_header.h           # ADTS sync & header unpacker/packer
│   ├── mp4/
│   │   ├── mp4_demuxer.h           # Stream-oriented atom reader & sample extractor
│   │   ├── mp4_muxer.h             # Fast M4A writer (ftyp, moov, mdat)
│   │   └── mp4_types.h             # Box/Atom definitions, AudioSpecificConfig
│   └── aac.h                       # Umbrella include for AAC
├── src/
│   ├── aac/
│   │   ├── aac_tables.cpp          # Huffman codebooks, scalefactor bands, KBD/Sine windows
│   │   ├── aac_mdct.cpp            # Forward MDCT (1024 & 128) & IMDCT
│   │   ├── adts_parser.cpp         # ADTS frame framing & CRC validation
│   │   ├── decoder/
│   │   │   ├── huffman_decoder.cpp # Spectral coefficient decoding (11 codebooks + escape)
│   │   │   ├── requantizer.cpp     # Scalefactor DPCM & non-linear dequantization (x^(4/3))
│   │   │   ├── stereo_processor.cpp# M/S stereo, intensity stereo, PNS
│   │   │   ├── tns_decoder.cpp     # Temporal noise shaping filter
│   │   │   └── aac_decoder.cpp     # Main decoder coordinator
│   │   └── encoder/
│   │       ├── transient_detector.cpp # Long vs 8-short window decision
│   │       ├── psychoacoustic.cpp  # FFT, Bark band energy, tonality & masking thresholds
│   │       ├── quantizer.cpp       # Dual-mode quantizer (Fast MCU single-pass & 2-loop search)
│   │       ├── huffman_encoder.cpp # Optimal codebook selection & bit packing
│   │       └── aac_encoder.cpp     # Main encoder coordinator
│   └── mp4/
│       ├── mp4_demuxer.cpp         # Streaming box parser (ftyp, moov, trak, stbl, esds)
│       └── mp4_muxer.cpp           # Fast M4A container generator
└── tests/
    ├── test_aac_tables.cpp
    ├── test_aac_mdct.cpp
    ├── test_aac_huffman.cpp
    ├── test_aac_decoder.cpp
    ├── test_aac_encoder.cpp
    ├── test_aac_roundtrip.cpp
    ├── test_adts_framing.cpp
    └── test_mp4_container.cpp
```

---

## 4. Subsystem Components & DSP Pipelines

### 4.1 ADTS Framing & Bitstream Representation
* **Header Structure**: 56-bit (7 bytes without CRC) or 72-bit (9 bytes with CRC).
  * Syncword: 12-bit `0xFFF`.
  * Layer: 2-bit `00`.
  * Profile: 2-bit `01` (AAC-LC).
  * Sampling Frequency Index: 4 bits ($0\text{--}11$).
  * Channel Configuration: 3 bits ($1 = \text{Mono}, 2 = \text{Stereo}$).
  * Frame Length: 13 bits (header + CRC + raw payload).
  * Buffer Fullness: 11 bits (`0x7FF` for VBR).
* **CRC-16**: Polynomial $0x8005$ ($x^{16} + x^{15} + x^2 + 1$) protecting the ADTS header and raw data block if `protection_absent == 0`.

### 4.2 Core AAC-LC Decoder
```
[ADTS Input] ──> [BitReader Unpacker] ──> [Syntactic Elements (SCE / CPE / FIL / END)]
                      │
                      ├──> [DPCM Scalefactor Decoder]
                      ├──> [Huffman 4-tuple / 2-tuple / Escape Spectral Unpacker]
                      ├──> [Non-linear Requantizer: X_inv = sign(X)*|X|^(4/3)*2^(0.25*(sf-100))]
                      ├──> [M/S & Intensity Stereo Processor]
                      ├──> [TNS (Temporal Noise Shaping) All-Pole IIR Filter]
                      ├──> [IMDCT + Sine / KBD Windowing]
                      └──> [Overlap-Add Buffer (1024 history)] ──> [PCM Out]
```

* **MDCT / IMDCT Filterbank**:
  * Long window: 2048 samples $\to$ 1024 spectral lines.
  * Short window: 8 $\times$ 256 samples $\to$ 8 $\times$ 128 spectral lines.
  * Window shapes: **Sine window** and **KBD (Kaiser-Bessel Derived) window** ($\alpha = 4$ for long, $\alpha = 6$ for short).
  * Fast algorithm: Pre-twiddle $\to$ $N/4$-point complex FFT $\to$ Post-twiddle.
* **Spectral Dequantization**:
  * Utilizes a static lookup table $X^{4/3}$ for $0 \le X \le 256$ to accelerate MCU computation without calling `powf()`.

### 4.3 Core AAC-LC Encoder
```
[PCM In] ──┬──> [Transient Detector (8 sub-band energy)] ──> [Window Sequence Decision]
           │
           ├──> [Psychoacoustic Model (2048 FFT)] ─────────> [Bark Masking Thresholds]
           │                                                               │
           └──> [Forward MDCT + Windowing] ──> [Quantizer (Fast MCU / 2-Loop)]
                                                              │
                                               [Huffman Codebook Selector]
                                                              │
                                              [ADTS / Stream Bitstream Packer] ──> [AAC Out]
```

* **Transient Detection**: Evaluates energy jump ratio across 8 sub-blocks to trigger `ONLY_LONG`, `LONG_START`, `EIGHT_SHORT`, or `LONG_STOP` window transitions.
* **Psychoacoustic Model**:
  * Computes 2048-point FFT with Hanning window.
  * Maps spectral energy into AAC scalefactor bands (Bark scale).
  * Computes spectral tonality index and applies spreading matrix to obtain per-band masking thresholds.
* **Quantizer Modes**:
  1. **Fast MCU Mode**: Single-pass global gain estimation based on perceptual entropy; scalefactors calculated directly from masking threshold power.
  2. **High-Quality Mode**: Nested two-loop search (Outer loop: distortion limit vs threshold; Inner loop: bit budget vs Huffman bit length).

### 4.4 MP4 / ISOBMFF Demuxer & Muxer (`.m4a`)
* **`mp4_demuxer`**:
  * Stream-oriented parsing of `ftyp`, `moov`, `mvhd`, `trak`, `tkhd`, `mdia`, `mdhd`, `hdlr`, `minf`, `smhd`, `stbl`, `stsd` (`mp4a`/`esds`), `stts`, `stsz`, `stsc`, `stco`/`co64`.
  * Zero-memory copy sample reader: seeks directly to frame payloads inside `mdat` based on chunk indices.
  * Extracted format metadata: `AudioSpecificConfig` (sample rate, channels, object type).
* **`mp4_muxer`**:
  * Streaming generator: writes `ftyp`, streams AAC frames to `mdat`, updates compact chunk index, and writes `moov` at stream closure.

---

## 5. Microcontroller Memory & Performance Budget

Target: **Teensy 4.1 (600 MHz Cortex-M7)** and **NXP i.MX RT1176 (1 GHz Cortex-M7)**.

| Component | RAM Budget (DTCM) | Flash (ROM) | Estimated CPU Load (Teensy 4.1) |
| :--- | :--- | :--- | :--- |
| **AAC Decoder** | $\approx 20\text{--}24\text{ KB}$ | $\approx 16\text{ KB}$ | $3\%\text{--}6\%$ |
| **AAC Encoder (Fast MCU)** | $\approx 32\text{--}40\text{ KB}$ | $\approx 20\text{ KB}$ | $15\%\text{--}25\%$ |
| **MP4 Demuxer / Muxer** | $\approx 4\text{--}8\text{ KB}$ | $\approx 8\text{ KB}$ | $< 1\%$ |

* **Zero Dynamic Allocation in Hot Loop**: All per-frame buffers (MDCT, FFT scratch, Overlap-Add, Huffman buffers) are statically allocated or pre-allocated inside decoder/encoder class instances.
* **Single-Precision Hardware FPU**: All DSP pipelines use `float` (`float32_t`) to leverage single-cycle Cortex-M7 FPU instructions (`VADD.F32`, `VMUL.F32`, `VFMA.F32`).

---

## 6. Verification and Testing Plan

1. **Unit Tests**:
   * `test_aac_tables`: Verifies Huffman codebook invertibility, KBD window symmetry, and scalefactor band tables.
   * `test_aac_mdct`: Verifies forward and inverse MDCT with TDAC perfect reconstruction for both long (1024) and short (128) transforms.
   * `test_adts_framing`: Header packing/unpacking, CRC-16 computation, corrupted stream recovery.
   * `test_aac_huffman`: 4-tuple, 2-tuple, and escape value coding across all 11 codebooks.
   * `test_mp4_container`: Muxes raw AAC frames into `.m4a` and demuxes them back to verify byte-exact stream extraction and `AudioSpecificConfig` parsing.
2. **Integration & Roundtrip Tests**:
   * `test_aac_roundtrip`: Full pipeline encode $\to$ decode roundtrip on synthetic waveforms (sine wave, chirp, multi-tone) checking SNR ($> 60\text{ dB}$) and THD+N.
   * `test_real_aac`: Conformance testing against reference bitstreams.
