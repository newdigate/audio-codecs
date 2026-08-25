# audio-codecs

Zero-allocation, clean-room C++17 implementations of AIFF/AIFC, WAV (RIFF), MP3 (ISO/IEC 11172-3 Layer III), FLAC (RFC 9639), AAC (ISO/IEC 14496-3), and Ogg/Vorbis I (RFC 3533 & Xiph Vorbis I specification) audio encoders and decoders under the MIT license.

## Features

- **No runtime dependencies**: Pure C++17 without external libraries.
- **Embedded-friendly**: Zero heap allocations during steady-state frame encode/decode (static pre-allocated state buffers).
- **Polymorphic interfaces**: Unified `AudioEncoder` and `AudioDecoder` base classes.
- **AIFF / AIFF-C**: Full Apple Audio Interchange File Format and Compressed AIFC support:
  - 8-bit signed PCM (two's complement), 16/24/32-bit Big-Endian PCM.
  - AIFC `sowt` Little-Endian PCM, `fl32` 32-bit IEEE float, and ITU-T G.711 `alaw`/`ulaw`.
  - IEEE 754 80-bit SANE extended precision float sample rate conversions.
  - Streaming `AiffDecoder` and `AiffEncoder` with re-entrant chunk scanner.
- **WAV**: Full Microsoft RIFF/WAVE container support for 8/16/24/32-bit PCM, IEEE float, ITU-T G.711 A-law/$\mu$-law, and `WAVE_FORMAT_EXTENSIBLE` multi-channel speaker masks.
- **Ogg**: RFC 3533 bitstream framing container, streaming `OggDemuxer` with packet assembly and CRC-32 polynomial verification, and `OggMuxer` page segmenter.
- **Vorbis I**: Full Vorbis I audio codec compliant with Xiph Vorbis I specification:
  - Canonical Huffman codebook engine with lookup types 0, 1, and 2.
  - Fast $O(N \log N)$ Forward MDCT and Inverse IMDCT with TDAC sine-of-sine windowing.
  - Floor 1 piecewise linear Bark-scale spectral envelope synthesis.
  - Residue types 0, 1, and 2 with polar channel coupling / stereo decoupling (Mapping 0).
  - Mode switching between short (512) and long (2048) block sizes.
  - Streaming `VorbisDecoder` and `VorbisEncoder` with Ogg page integration.
- **MP3**: Full MPEG-1 Layer III encoder (MDCT, psychoacoustic model, Huffman) and decoder (Huffman, requantization, IMDCT, synthesis filter).
- **FLAC**: Lossless encoder and decoder supporting Fixed/LPC prediction (Levinson-Durbin up to order 32), Rice coding, Mid/Side stereo decorrelation, CRC-8/CRC-16 validation, and MD5 streaming checksums. Bit-exact integer (`int16_t`, `int32_t`) and normalized float (`float`) I/O.

## Quick Start

### Ogg / Vorbis

#### Decoding
```cpp
#include "audio_codecs/vorbis.h"

audio_codecs::vorbis::VorbisDecoder decoder;
decoder.init({});

// Feed incoming Ogg bitstream pages or raw Vorbis packets
float pcm_out[8192];
int samples = decoder.decode_frame(ogg_data, ogg_size, pcm_out, sizeof(pcm_out) / sizeof(float));
if (samples > 0) {
    // Process decoded interleaved float PCM samples
}
```

#### Encoding
```cpp
#include "audio_codecs/vorbis.h"

audio_codecs::vorbis::VorbisEncoder encoder;
audio_codecs::AudioConfig config;
config.channels = 2;
config.sample_rate = 44100;
config.bitrate_kbps = 128;
encoder.init(config);

uint8_t ogg_out[65536];
int bytes = encoder.encode_frame(in_pcm, num_samples, ogg_out, sizeof(ogg_out));

// Flush at end of stream
int flush_bytes = encoder.flush(ogg_out + bytes, sizeof(ogg_out) - bytes);
```

---

### MP3

#### Decoding
```cpp
#include "audio_codecs/mp3/mp3_decoder.h"

audio_codecs::mp3::Mp3Decoder decoder;
decoder.init({});

float pcm_out[2304]; // Interleaved samples (1152 * channels)
int samples = decoder.decode_frame(mp3_data, mp3_size, pcm_out, 2304);
if (samples > 0) {
    // Process decoded samples
}
```

#### Encoding
```cpp
#include "audio_codecs/mp3/mp3_encoder.h"

audio_codecs::mp3::Mp3Encoder encoder;
audio_codecs::AudioConfig config{44100, 2, 128}; // 44.1 kHz, stereo, 128 kbps
encoder.init(config);

uint8_t mp3_out[4096];
int bytes = encoder.encode_frame(pcm_float_data, 1152 * 2, mp3_out, sizeof(mp3_out));
```

---

### FLAC

#### Decoding
```cpp
#include "audio_codecs/flac/flac_decoder.h"

audio_codecs::flac::FlacDecoder decoder;

// Parse "fLaC" container & STREAMINFO header
size_t bytes_consumed = 0;
decoder.parse_stream_header(flac_data, flac_size, bytes_consumed);

// Decode frame to 16-bit integer PCM
int16_t pcm_out[4096 * 2];
int samples = decoder.decode_frame_i16(flac_data + bytes_consumed, flac_size - bytes_consumed, pcm_out, 4096 * 2);
```

#### Encoding
```cpp
#include "audio_codecs/flac/flac_encoder.h"

audio_codecs::flac::FlacEncoder encoder;
audio_codecs::flac::FlacEncoderConfig config;
config.core_config.sample_rate = 44100;
config.core_config.channels = 2;
config.compression_level = 5; // 0 (fastest) to 8 (max LPC search)
config.block_size = 4096;
encoder.init_flac(config);

// Write 42-byte "fLaC" + STREAMINFO header
uint8_t header[42];
int hdr_bytes = encoder.write_stream_header(header, sizeof(header));

// Encode frame
uint8_t flac_out[65536];
int bytes = encoder.encode_frame_i16(pcm_16bit, 4096 * 2, flac_out, sizeof(flac_out));
```

---

### AIFF / AIFF-C

#### Decoding
```cpp
#include "audio_codecs/aiff/aiff_decoder.h"

audio_codecs::aiff::AiffDecoder decoder;

// Incrementally parse FORM container & COMM header
size_t bytes_consumed = 0;
decoder.parse_stream_header(aiff_data, aiff_size, bytes_consumed);

// Decode frame to 16-bit integer PCM or normalized float
int16_t pcm_out[4096 * 2];
int samples = decoder.decode_frame_i16(aiff_data + bytes_consumed, aiff_size - bytes_consumed, pcm_out, 4096 * 2);
```

#### Encoding
```cpp
#include "audio_codecs/aiff/aiff_encoder.h"

audio_codecs::aiff::AiffEncoder encoder;
audio_codecs::aiff::AiffEncoderConfig config;
config.core_config.sample_rate = 44100;
config.core_config.channels = 2;
config.sample_format = audio_codecs::aiff::AiffSampleFormat::Int16BE; // Standard Big-Endian PCM
encoder.init_aiff(config);

// Write header
uint8_t header[128];
int hdr_bytes = encoder.write_stream_header(header, sizeof(header));

// Encode audio frames
uint8_t aiff_out[65536];
int bytes = encoder.encode_frame_i16(pcm_16bit, num_samples, aiff_out, sizeof(aiff_out));

// Finalize header in-place with total data byte count
encoder.finalize_header(header, bytes);
```

## Building & Testing

Requires a C++17 compatible compiler and CMake 3.16+.

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

[MIT](LICENSE)
