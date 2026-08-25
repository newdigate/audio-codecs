# audio-codecs

Zero-allocation, clean-room C++17 implementations of MP3 (ISO/IEC 11172-3 Layer III) and FLAC (RFC 9639) audio encoders and decoders under the MIT license.

## Features

- **No runtime dependencies**: Pure C++17 without external libraries.
- **Embedded-friendly**: Zero heap allocations during steady-state frame encode/decode (static pre-allocated buffers).
- **Polymorphic interfaces**: Unified `AudioEncoder` and `AudioDecoder` base classes.
- **MP3**: Full MPEG-1 Layer III encoder (MDCT, psychoacoustic model, Huffman) and decoder (Huffman, requantization, IMDCT, synthesis filter).
- **FLAC**: Lossless encoder and decoder supporting Fixed/LPC prediction (Levinson-Durbin up to order 32), Rice coding, Mid/Side stereo decorrelation, CRC-8/CRC-16 validation, and MD5 streaming checksums. Bit-exact integer (`int16_t`, `int32_t`) and normalized float (`float`) I/O.

## Quick Start

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

## Building & Testing

Requires a C++17 compatible compiler and CMake 3.16+.

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

[MIT](LICENSE)
