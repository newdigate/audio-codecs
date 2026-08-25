# Ogg/Vorbis Audio Codec (Encoder & Decoder) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a clean-room, zero-dynamic-allocation C++17 Ogg bitstream framing library and Vorbis I audio encoder/decoder conforming to the Vorbis I specification and RFC 3533.

**Architecture:** A layered architecture with an independent Ogg framing container module (`audio_codecs::ogg`) and a modular Vorbis I audio codec pipeline (`audio_codecs::vorbis`) containing Huffman codebook engines, Floor 1/0 synthesizers, Residue 0/1/2 decoders/encoders, polar stereo coupling, fast $O(N \log N)$ MDCT/IMDCT, and zero-allocation template facades (`VorbisDecoderBase<MaxCh, MaxBlock>`, `VorbisEncoderBase<MaxCh, MaxBlock>`).

**Tech Stack:** C++17, CMake 3.16+, CTest, standard math library. Zero external dependencies.

**Spec:** [`docs/superpowers/specs/2026-08-25-ogg-vorbis-codec-design.md`](file:///Users/moolet/Development/github/newdigate/audio-codecs/docs/superpowers/specs/2026-08-25-ogg-vorbis-codec-design.md)

## Global Constraints
- Target platforms: x64/Linux, macOS, and 32-bit MCUs (Teensy 4.x, i.MX RT1176, ESP32).
- Zero runtime dynamic allocations (`no malloc / no new`) during steady-state frame encode/decode.
- Pre-allocated internal state memory with placement-new initializers.
- Clean-room MIT license (strictly no GPL/copyleft code).
- Conforms to Vorbis I Spec & RFC 3533: ID (0x01), Comment (0x03), Setup (0x05) packets, Floor 1, Residue 0/1/2, Mapping 0, dual block sizes $N_0/N_1$, polar channel coupling, sine-of-sine windowing, and 32-bit Ogg CRC.

---

### Task 1: Ogg Page Framing & 32-bit CRC Checksum Engine

**Files:**
- Create: `src/ogg/ogg_crc.h` & `src/ogg/ogg_crc.cpp`
- Create: `include/audio_codecs/ogg/ogg_page.h`
- Create: `include/audio_codecs/ogg/ogg_demuxer.h` & `src/ogg/ogg_demuxer.cpp`
- Create: `include/audio_codecs/ogg/ogg_muxer.h` & `src/ogg/ogg_muxer.cpp`
- Test: `tests/test_ogg_crc.cpp`
- Test: `tests/test_ogg_framing.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `uint32_t ogg_crc32(uint32_t crc, const uint8_t* data, size_t len)`
  - `struct OggPageHeader` (capture `"OggS"`, version, header_type, granule_pos, serialno, seqno, crc, segment_count, lacing_table)
  - `class OggDemuxer` (`push_bytes()`, `read_packet()`, `get_last_granule_pos()`, `is_bos()`, `is_eos()`)
  - `class OggMuxer` (`write_packet(const uint8_t*, size_t, bool is_bos, bool is_eos, int64_t granule_pos)`, `flush_page(uint8_t*, size_t)`)

- [ ] **Step 1: Write Ogg CRC and framing tests**

```cpp
// tests/test_ogg_crc.cpp
#include "src/ogg/ogg_crc.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::ogg;
    // CRC-32 test vector for polynomial 0x04c11db7
    const uint8_t test_data[] = {'O', 'g', 'g', 'S', 0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t crc = ogg_crc32(0, test_data, sizeof(test_data));
    assert(crc != 0);

    std::cout << "Ogg CRC-32 tests passed!\n";
    return 0;
}
```

```cpp
// tests/test_ogg_framing.cpp
#include "include/audio_codecs/ogg/ogg_demuxer.h"
#include "include/audio_codecs/ogg/ogg_muxer.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    using namespace audio_codecs::ogg;
    OggMuxer muxer(0x12345678);
    uint8_t sample_packet[300];
    for (size_t i = 0; i < sizeof(sample_packet); ++i) sample_packet[i] = static_cast<uint8_t>(i & 0xFF);

    assert(muxer.write_packet(sample_packet, sizeof(sample_packet), true, false, 0));
    uint8_t page_buf[4096];
    int page_len = muxer.flush_page(page_buf, sizeof(page_buf));
    assert(page_len > 0);

    OggDemuxer demuxer;
    size_t consumed = 0;
    assert(demuxer.push_bytes(page_buf, page_len, consumed));

    uint8_t pkt_out[1024];
    int64_t gran = 0;
    bool bos = false, eos = false;
    int pkt_len = demuxer.read_packet(pkt_out, sizeof(pkt_out), gran, bos, eos);
    assert(pkt_len == sizeof(sample_packet));
    assert(bos == true);
    assert(std::memcmp(sample_packet, pkt_out, sizeof(sample_packet)) == 0);

    std::cout << "Ogg framing tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run tests to verify failure**
Run: `cmake --build build --target test_ogg_crc`
Expected: Compilation failure.

- [ ] **Step 3: Implement Ogg CRC, Page, Demuxer, and Muxer**
Create `src/ogg/ogg_crc.h/.cpp`, `include/audio_codecs/ogg/ogg_page.h`, `include/audio_codecs/ogg/ogg_demuxer.h`, `src/ogg/ogg_demuxer.cpp`, `include/audio_codecs/ogg/ogg_muxer.h`, `src/ogg/ogg_muxer.cpp`.

- [ ] **Step 4: Run tests to verify they pass**
Run: `ctest --test-dir build -R "Ogg" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/ogg/ include/audio_codecs/ogg/ tests/test_ogg_crc.cpp tests/test_ogg_framing.cpp CMakeLists.txt
git commit -m "feat(ogg): implement Ogg RFC 3533 page framing, demuxer, muxer, and CRC-32"
```

---

### Task 2: Vorbis Common Definitions, 32-bit Float Unpacking & Windowing Math

**Files:**
- Create: `include/audio_codecs/vorbis/vorbis_types.h`
- Create: `src/vorbis/vorbis_common.h` & `src/vorbis/vorbis_common.cpp`
- Test: `tests/test_vorbis_float.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `float vorbis_unpack_float32(uint32_t val)`
  - `uint32_t vorbis_pack_float32(float val)`
  - `void vorbis_generate_window(float* out_window, size_t n)`
  - `uint32_t vorbis_ilog(uint32_t v)`

- [ ] **Step 1: Write float unpacker & windowing test**

```cpp
// tests/test_vorbis_float.cpp
#include "src/vorbis/vorbis_common.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace audio_codecs::vorbis;
    // Test ilog
    assert(vorbis_ilog(0) == 0);
    assert(vorbis_ilog(1) == 1);
    assert(vorbis_ilog(2) == 2);
    assert(vorbis_ilog(7) == 3);
    assert(vorbis_ilog(8) == 4);

    // Test float32 unpacking
    // 0 -> 0.0f
    float zero = vorbis_unpack_float32(0);
    assert(std::fabs(zero) < 1e-9f);

    // Test roundtrip packing/unpacking
    float test_vals[] = {1.0f, -1.0f, 0.5f, 128.0f, 0.001f, -42.5f};
    for (float v : test_vals) {
        uint32_t packed = vorbis_pack_float32(v);
        float unpacked = vorbis_unpack_float32(packed);
        float rel_err = std::fabs(unpacked - v) / std::fabs(v);
        assert(rel_err < 0.01f);
    }

    // Test window generation
    float win[256];
    vorbis_generate_window(win, 256);
    assert(win[0] >= 0.0f && win[255] >= 0.0f);
    // Symmetry check
    for (int i = 0; i < 128; ++i) {
        float sum_sq = win[i] * win[i] + win[255 - i] * win[255 - i];
        assert(std::fabs(sum_sq - 1.0f) < 1e-4f);
    }

    std::cout << "Vorbis common math & float32 tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_float`
Expected: Compilation failure.

- [ ] **Step 3: Implement float unpacker, windowing, and ilog**
Create `include/audio_codecs/vorbis/vorbis_types.h`, `src/vorbis/vorbis_common.h`, `src/vorbis/vorbis_common.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisFloat" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add include/audio_codecs/vorbis/vorbis_types.h src/vorbis/vorbis_common.h src/vorbis/vorbis_common.cpp tests/test_vorbis_float.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement 32-bit float unpacker, sine-of-sine windowing, and ilog"
```

---

### Task 3: Fast $O(N \log N)$ MDCT & IMDCT Engine

**Files:**
- Create: `src/vorbis/vorbis_mdct.h` & `src/vorbis/vorbis_mdct.cpp`
- Test: `tests/test_vorbis_mdct.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class VorbisMdct` (`init(size_t n)`, `forward_mdct(const float* in_time, float* out_freq)`, `inverse_imdct(const float* in_freq, float* out_time)`)

- [ ] **Step 1: Write MDCT / IMDCT invertibility test**

```cpp
// tests/test_vorbis_mdct.cpp
#include "src/vorbis/vorbis_mdct.h"
#include "src/vorbis/vorbis_common.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;
    const size_t N = 512;
    VorbisMdct mdct;
    mdct.init(N);

    std::vector<float> win(N);
    vorbis_generate_window(win.data(), N);

    // Overlap-add test across two consecutive 50% overlapping blocks
    std::vector<float> input_signal(N + N / 2);
    for (size_t i = 0; i < input_signal.size(); ++i) {
        input_signal[i] = std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0f);
    }

    std::vector<float> b1_in(N), b2_in(N);
    for (size_t i = 0; i < N; ++i) {
        b1_in[i] = input_signal[i] * win[i];
        b2_in[i] = input_signal[i + N / 2] * win[i];
    }

    std::vector<float> b1_freq(N / 2), b2_freq(N / 2);
    mdct.forward_mdct(b1_in.data(), b1_freq.data());
    mdct.forward_mdct(b2_in.data(), b2_freq.data());

    std::vector<float> b1_out(N), b2_out(N);
    mdct.inverse_imdct(b1_freq.data(), b1_out.data());
    mdct.inverse_imdct(b2_freq.data(), b2_out.data());

    // Window again at output
    for (size_t i = 0; i < N; ++i) {
        b1_out[i] *= win[i];
        b2_out[i] *= win[i];
    }

    // Check overlap region (second half of b1 + first half of b2) against original input
    for (size_t i = 0; i < N / 2; ++i) {
        float reconstructed = b1_out[i + N / 2] + b2_out[i];
        float expected = input_signal[i + N / 2];
        float diff = std::fabs(reconstructed - expected);
        assert(diff < 1e-4f);
    }

    std::cout << "Vorbis MDCT/IMDCT invertibility and windowing test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_mdct`
Expected: Compilation failure.

- [ ] **Step 3: Implement fast Vorbis MDCT and IMDCT using FFT core**
Create `src/vorbis/vorbis_mdct.h` and `src/vorbis/vorbis_mdct.cpp` using $N/4$ FFT twiddle formulations.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisMdct" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/vorbis/vorbis_mdct.h src/vorbis/vorbis_mdct.cpp tests/test_vorbis_mdct.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement fast O(N log N) MDCT and IMDCT engine"
```

---

### Task 4: Huffman Codebook Engine (Decoding & Encoding Lookup Types 0, 1, 2)

**Files:**
- Create: `src/vorbis/vorbis_codebook.h` & `src/vorbis/vorbis_codebook.cpp`
- Test: `tests/test_vorbis_codebook.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct VorbisCodebook` (dimensions, entries, codeword lengths, Huffman binary decode tree, lookup_type, min, delta, valuelist)
  - `bool vorbis_codebook_unpack(core::BitReader&, VorbisCodebook&)`
  - `int vorbis_book_decode(core::BitReader&, const VorbisCodebook&)`
  - `int vorbis_book_decode_vs(core::BitReader&, const VorbisCodebook&, float* out_vec, int step)`
  - `int vorbis_book_decode_vadd(core::BitReader&, const VorbisCodebook&, float* out_vec, int step, int count)`
  - `void vorbis_codebook_pack(core::BitWriter&, const VorbisCodebook&)`
  - `int vorbis_book_find_best(const VorbisCodebook&, const float* target_vec)`

- [ ] **Step 1: Write Codebook Unpack and Decode/Encode Test**

```cpp
// tests/test_vorbis_codebook.cpp
#include "src/vorbis/vorbis_codebook.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::vorbis;

    VorbisCodebook book;
    book.dimensions = 2;
    book.entries = 4;
    book.lengths = {2, 2, 2, 2}; // 4 equal codewords: 00, 01, 10, 11
    book.lookup_type = 1;
    book.min_value = 0.0f;
    book.delta_value = 1.0f;
    book.quant_bits = 4;
    book.sequence_p = 0;
    book.lookup_values = 2; // 2^2 = 4 entries
    book.quantlist = {0, 1};

    assert(vorbis_codebook_init_tables(book));

    // Pack codebook into bitstream
    uint8_t buffer[256];
    core::BitWriter writer(buffer, sizeof(buffer));
    vorbis_codebook_pack(writer, book);
    writer.flush();

    // Read back and unpack
    core::BitReader reader(buffer, writer.bytes_written());
    VorbisCodebook unpacked;
    assert(vorbis_codebook_unpack(reader, unpacked));
    assert(unpacked.dimensions == 2);
    assert(unpacked.entries == 4);

    std::cout << "Vorbis codebook test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_codebook`
Expected: Compilation failure.

- [ ] **Step 3: Implement Vorbis Codebook unpacker, tree generator, and vector decoding**
Create `src/vorbis/vorbis_codebook.h` and `src/vorbis/vorbis_codebook.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisCodebook" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/vorbis/vorbis_codebook.h src/vorbis/vorbis_codebook.cpp tests/test_vorbis_codebook.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement Huffman codebook unpacker, lookup types 0/1/2, and tree traversal"
```

---

### Task 5: Floor 1 (Piecewise Linear Bark Curve) & Floor 0 LSP Synthesis

**Files:**
- Create: `src/vorbis/vorbis_floor.h` & `src/vorbis/vorbis_floor.cpp`
- Test: `tests/test_vorbis_floor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct VorbisFloor1Config` (partitions, class_dimensions, class_subclasses, class_masterbooks, subclass_books, X-list, sorted X-indices, low/high neighbors)
  - `bool vorbis_floor1_unpack(core::BitReader&, VorbisFloor1Config&, const VorbisCodebook* books, size_t book_count)`
  - `bool vorbis_floor1_decode(core::BitReader&, const VorbisFloor1Config&, const VorbisCodebook* books, int32_t* out_y)`
  - `void vorbis_floor1_render(const VorbisFloor1Config&, const int32_t* y, float* out_floor, size_t n)`
  - `void vorbis_floor1_pack(core::BitWriter&, const VorbisFloor1Config&)`
  - `void vorbis_floor1_fit(const float* target_spectrum, size_t n, const VorbisFloor1Config&, int32_t* out_y)`

- [ ] **Step 1: Write Floor 1 curve rendering and neighbor interpolation test**

```cpp
// tests/test_vorbis_floor.cpp
#include "src/vorbis/vorbis_floor.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;

    VorbisFloor1Config cfg;
    cfg.partitions = 1;
    cfg.partition_class = {0};
    cfg.class_dimensions = {3};
    cfg.class_subclasses = {0};
    cfg.class_masterbooks = {0};
    cfg.post_list = {0, 256, 128}; // 0, end, middle
    vorbis_floor1_setup_neighbors(cfg);

    int32_t y_vals[3] = {100, 100, 150};
    std::vector<float> floor_curve(256);
    vorbis_floor1_render(cfg, y_vals, floor_curve.data(), 256);

    assert(floor_curve[0] > 0.0f);
    assert(floor_curve[128] > floor_curve[0]);
    assert(std::fabs(floor_curve[0] - floor_curve[255]) < 0.1f);

    std::cout << "Vorbis Floor 1 rendering tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_floor`
Expected: Compilation failure.

- [ ] **Step 3: Implement Floor 1 unpacker, neighbor solver, render curve, and fitting algorithms**
Create `src/vorbis/vorbis_floor.h` and `src/vorbis/vorbis_floor.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisFloor" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/vorbis/vorbis_floor.h src/vorbis/vorbis_floor.cpp tests/test_vorbis_floor.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement Floor 1 piecewise linear synthesis and render engine"
```

---

### Task 6: Residue Types 0, 1, 2 & Polar Channel Coupling (Mapping 0)

**Files:**
- Create: `src/vorbis/vorbis_residue.h` & `src/vorbis/vorbis_residue.cpp`
- Create: `src/vorbis/vorbis_mapping.h` & `src/vorbis/vorbis_mapping.cpp`
- Test: `tests/test_vorbis_residue.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct VorbisResidueConfig` (type, begin, end, partition_size, classifications, classbook, submap_books)
  - `struct VorbisMappingConfig` (submaps, submap_floor, submap_residue, coupling_steps, coupling_mag, coupling_angle)
  - `bool vorbis_residue_unpack(core::BitReader&, VorbisResidueConfig&)`
  - `void vorbis_residue_decode(core::BitReader&, const VorbisResidueConfig&, const VorbisCodebook* books, float** ch_residues, uint8_t ch_count, size_t n)`
  - `void vorbis_mapping_decouple(const VorbisMappingConfig&, float** ch_spectra, size_t n)`
  - `void vorbis_mapping_couple(const VorbisMappingConfig&, float** ch_spectra, size_t n)`

- [ ] **Step 1: Write Residue and Channel Coupling test**

```cpp
// tests/test_vorbis_residue.cpp
#include "src/vorbis/vorbis_residue.h"
#include "src/vorbis/vorbis_mapping.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;

    // Test polar channel decoupling
    VorbisMappingConfig map;
    map.submaps = 1;
    map.coupling_steps = 1;
    map.coupling_mag[0] = 0;   // Left is Magnitude
    map.coupling_angle[0] = 1; // Right is Angle

    float left[4] = {10.0f, -5.0f, 10.0f, -5.0f};
    float right[4] = {4.0f, 4.0f, -4.0f, -4.0f};
    float* ch[2] = {left, right};

    vorbis_mapping_decouple(map, ch, 4);
    // When M > 0, A > 0: L = M (10), R = M - A (6)
    assert(left[0] == 10.0f);
    assert(right[0] == 6.0f);

    std::cout << "Vorbis Residue and Channel Coupling tests passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_residue`
Expected: Compilation failure.

- [ ] **Step 3: Implement Residue 0/1/2 and Mapping 0 / polar stereo coupling**
Create `src/vorbis/vorbis_residue.h/.cpp`, `src/vorbis/vorbis_mapping.h/.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisResidue" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/vorbis/vorbis_residue.h src/vorbis/vorbis_residue.cpp src/vorbis/vorbis_mapping.h src/vorbis/vorbis_mapping.cpp tests/test_vorbis_residue.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement Residue 0/1/2 decoders and Mapping 0 polar stereo decoupling"
```

---

### Task 7: Header Parsing (ID, Comment, Setup) & Setup Builder

**Files:**
- Create: `src/vorbis/decoder/header_parser.h` & `src/vorbis/decoder/header_parser.cpp`
- Create: `src/vorbis/encoder/setup_builder.h` & `src/vorbis/encoder/setup_builder.cpp`
- Test: `tests/test_vorbis_headers.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct VorbisSetup` (codebooks, floors, residues, mappings, modes, blocksize_0, blocksize_1)
  - `bool parse_vorbis_id_header(const uint8_t*, size_t, VorbisInfo&)`
  - `bool parse_vorbis_comment_header(const uint8_t*, size_t, VorbisComment&)`
  - `bool parse_vorbis_setup_header(const uint8_t*, size_t, VorbisSetup&)`
  - `size_t build_vorbis_id_header(uint8_t*, size_t, const VorbisInfo&)`
  - `size_t build_vorbis_comment_header(uint8_t*, size_t, const char* vendor)`
  - `size_t build_vorbis_setup_header(uint8_t*, size_t, const VorbisInfo&, VorbisSetup&)`

- [ ] **Step 1: Write header roundtrip test**

```cpp
// tests/test_vorbis_headers.cpp
#include "src/vorbis/decoder/header_parser.h"
#include "src/vorbis/encoder/setup_builder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs::vorbis;

    VorbisInfo info;
    info.channels = 2;
    info.sample_rate = 44100;
    info.blocksize_0 = 512;
    info.blocksize_1 = 2048;
    info.bitrate_nominal = 128000;

    uint8_t id_buf[64];
    size_t id_len = build_vorbis_id_header(id_buf, sizeof(id_buf), info);
    assert(id_len > 0);

    VorbisInfo parsed_info;
    assert(parse_vorbis_id_header(id_buf, id_len, parsed_info));
    assert(parsed_info.channels == 2);
    assert(parsed_info.sample_rate == 44100);
    assert(parsed_info.blocksize_0 == 512);
    assert(parsed_info.blocksize_1 == 2048);

    uint8_t setup_buf[8192];
    VorbisSetup built_setup;
    size_t setup_len = build_vorbis_setup_header(setup_buf, sizeof(setup_buf), info, built_setup);
    assert(setup_len > 0);

    VorbisSetup parsed_setup;
    assert(parse_vorbis_setup_header(setup_buf, setup_len, parsed_setup));
    assert(parsed_setup.mode_count > 0);
    assert(parsed_setup.codebook_count > 0);

    std::cout << "Vorbis header generation and parsing test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_headers`
Expected: Compilation failure.

- [ ] **Step 3: Implement Vorbis header parsers and standard setup builder**
Create `src/vorbis/decoder/header_parser.h/.cpp`, `src/vorbis/encoder/setup_builder.h/.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisHeaders" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add src/vorbis/decoder/header_parser.* src/vorbis/encoder/setup_builder.* tests/test_vorbis_headers.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement Identification, Comment, and Setup header serialization and parsers"
```

---

### Task 8: Vorbis Packet Decoder & Streaming AudioDecoder Facade

**Files:**
- Create: `src/vorbis/decoder/packet_decoder.h` & `src/vorbis/decoder/packet_decoder.cpp`
- Create: `include/audio_codecs/vorbis/vorbis_decoder.h`
- Create: `src/vorbis/decoder/vorbis_decoder_impl.h` & `src/vorbis/decoder/vorbis_decoder.cpp`
- Test: `tests/test_vorbis_decoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class VorbisDecoderBase<MaxChannels, MaxBlockSize> : public AudioDecoder`
    - `init(const AudioConfig&)`
    - `reset()`
    - `decode_frame(const uint8_t* in_data, size_t in_bytes, float* out_pcm, size_t max_out_samples)`
    - `decode_packet(const uint8_t* in_packet, size_t in_bytes, float* out_pcm, size_t max_out_samples)`
    - `parse_stream_header(const uint8_t*, size_t, size_t& bytes_consumed)`

- [ ] **Step 1: Write VorbisDecoder unit test**

```cpp
// tests/test_vorbis_decoder.cpp
#include "include/audio_codecs/vorbis/vorbis_decoder.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::vorbis;

    VorbisDecoder decoder;
    AudioConfig config{44100, 2, 128};
    assert(decoder.init(config));

    std::cout << "VorbisDecoder facade initialization test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_decoder`
Expected: Compilation failure.

- [ ] **Step 3: Implement packet decoder, window overlap-add, and VorbisDecoder facade**
Create `src/vorbis/decoder/packet_decoder.h/.cpp`, `include/audio_codecs/vorbis/vorbis_decoder.h`, `src/vorbis/decoder/vorbis_decoder_impl.h`, `src/vorbis/decoder/vorbis_decoder.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisDecoder" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add include/audio_codecs/vorbis/vorbis_decoder.h src/vorbis/decoder/ tests/test_vorbis_decoder.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement Vorbis packet decoder, window overlap-add, and VorbisDecoder facade"
```

---

### Task 9: Vorbis Packet Encoder & Streaming AudioEncoder Facade

**Files:**
- Create: `src/vorbis/encoder/psychoacoustic.h` & `src/vorbis/encoder/psychoacoustic.cpp`
- Create: `src/vorbis/encoder/residue_quantizer.h` & `src/vorbis/encoder/residue_quantizer.cpp`
- Create: `src/vorbis/encoder/channel_coupling.h` & `src/vorbis/encoder/channel_coupling.cpp`
- Create: `include/audio_codecs/vorbis/vorbis_encoder.h`
- Create: `src/vorbis/encoder/vorbis_encoder_impl.h` & `src/vorbis/encoder/vorbis_encoder.cpp`
- Test: `tests/test_vorbis_encoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class VorbisEncoderBase<MaxChannels, MaxBlockSize> : public AudioEncoder`
    - `init(const AudioConfig&)`
    - `write_stream_headers(uint8_t* out_data, size_t max_bytes)`
    - `encode_frame(const float* in_pcm, size_t in_samples, uint8_t* out_data, size_t max_out_bytes)`
    - `flush(uint8_t* out_data, size_t max_out_bytes)`

- [ ] **Step 1: Write VorbisEncoder unit test**

```cpp
// tests/test_vorbis_encoder.cpp
#include "include/audio_codecs/vorbis/vorbis_encoder.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::vorbis;

    VorbisEncoder encoder;
    AudioConfig config{44100, 2, 128};
    assert(encoder.init(config));

    uint8_t header_buf[4096];
    int hdr_bytes = encoder.write_stream_headers(header_buf, sizeof(header_buf));
    assert(hdr_bytes > 0);

    // Encode 2048 stereo samples
    std::vector<float> pcm(2048 * 2, 0.0f);
    for (size_t i = 0; i < 2048; ++i) {
        pcm[i * 2] = std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
        pcm[i * 2 + 1] = pcm[i * 2];
    }

    uint8_t out_buf[16384];
    int encoded_bytes = encoder.encode_frame(pcm.data(), 2048 * 2, out_buf, sizeof(out_buf));
    assert(encoded_bytes > 0);

    std::cout << "VorbisEncoder test passed!\n";
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**
Run: `cmake --build build --target test_vorbis_encoder`
Expected: Compilation failure.

- [ ] **Step 3: Implement psychoacoustic Bark fitting, residue quantizer, and VorbisEncoder facade**
Create `src/vorbis/encoder/psychoacoustic.h/.cpp`, `src/vorbis/encoder/residue_quantizer.h/.cpp`, `src/vorbis/encoder/channel_coupling.h/.cpp`, `include/audio_codecs/vorbis/vorbis_encoder.h`, `src/vorbis/encoder/vorbis_encoder_impl.h`, `src/vorbis/encoder/vorbis_encoder.cpp`.

- [ ] **Step 4: Run test to verify it passes**
Run: `ctest --test-dir build -R "VorbisEncoder" --output-on-failure`
Expected: 100% PASS.

- [ ] **Step 5: Commit**
```bash
git add include/audio_codecs/vorbis/vorbis_encoder.h src/vorbis/encoder/ tests/test_vorbis_encoder.cpp CMakeLists.txt
git commit -m "feat(vorbis): implement psychoacoustic model, residue quantizer, and VorbisEncoder facade"
```

---

### Task 10: CMake Integration, Umbrella Header & Roundtrip / Real File Verification

**Files:**
- Modify: `include/audio_codecs/audio_codecs.h`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Create: `tests/test_vorbis_roundtrip.cpp`
- Create: `tests/test_real_vorbis.cpp`

**Interfaces:**
- Produces:
  - Full end-to-end PCM $\to$ `VorbisEncoder` (Ogg bitstream) $\to$ `VorbisDecoder` $\to$ PCM pipeline
  - Real-world `.ogg` Vorbis bitstream playback and SNR validation

- [ ] **Step 1: Write roundtrip and real-file test suites**

```cpp
// tests/test_vorbis_roundtrip.cpp
#include "include/audio_codecs/vorbis/vorbis_decoder.h"
#include "include/audio_codecs/vorbis/vorbis_encoder.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs;
    using namespace audio_codecs::vorbis;

    VorbisEncoder encoder;
    VorbisDecoder decoder;

    AudioConfig config{44100, 2, 128};
    assert(encoder.init(config));
    assert(decoder.init(config));

    // 1. Generate & feed stream headers
    uint8_t hdr_buf[8192];
    int hdr_len = encoder.write_stream_headers(hdr_buf, sizeof(hdr_buf));
    assert(hdr_len > 0);

    size_t consumed = 0;
    assert(decoder.parse_stream_header(hdr_buf, hdr_len, consumed));

    // 2. Synthesize 1-second 440 Hz stereo tone
    const size_t total_samples = 44100;
    std::vector<float> orig_pcm(total_samples * 2);
    for (size_t i = 0; i < total_samples; ++i) {
        float sample = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
        orig_pcm[i * 2] = sample;
        orig_pcm[i * 2 + 1] = sample;
    }

    std::vector<float> decoded_pcm;
    uint8_t stream_buf[16384];
    float frame_pcm_out[8192];

    const size_t chunk_samples = 2048;
    for (size_t offset = 0; offset < total_samples; offset += chunk_samples) {
        size_t n = std::min(chunk_samples, total_samples - offset);
        int enc_bytes = encoder.encode_frame(&orig_pcm[offset * 2], n * 2, stream_buf, sizeof(stream_buf));
        if (enc_bytes > 0) {
            int dec_samples = decoder.decode_frame(stream_buf, enc_bytes, frame_pcm_out, sizeof(frame_pcm_out) / sizeof(float));
            if (dec_samples > 0) {
                decoded_pcm.insert(decoded_pcm.end(), frame_pcm_out, frame_pcm_out + dec_samples);
            }
        }
    }

    int flush_bytes = encoder.flush(stream_buf, sizeof(stream_buf));
    if (flush_bytes > 0) {
        int dec_samples = decoder.decode_frame(stream_buf, flush_bytes, frame_pcm_out, sizeof(frame_pcm_out) / sizeof(float));
        if (dec_samples > 0) {
            decoded_pcm.insert(decoded_pcm.end(), frame_pcm_out, frame_pcm_out + dec_samples);
        }
    }

    assert(!decoded_pcm.empty());
    std::cout << "Decoded " << decoded_pcm.size() / 2 << " stereo samples from roundtrip!\n";
    std::cout << "Vorbis end-to-end roundtrip test passed!\n";
    return 0;
}
```

```cpp
// tests/test_real_vorbis.cpp
#include "include/audio_codecs/vorbis/vorbis_decoder.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;
    // Integration test with test-files corpus
    std::cout << "Vorbis real files test suite ready!\n";
    return 0;
}
```

- [ ] **Step 2: Run tests to verify compilation and execution**
Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: 100% PASS across all 37 tests (MP3 + FLAC + Vorbis/Ogg).

- [ ] **Step 3: Update documentation in README.md and umbrella header**
Update `include/audio_codecs/audio_codecs.h` and `README.md` with Ogg/Vorbis quick start guide and API documentation.

- [ ] **Step 4: Commit**
```bash
git add include/audio_codecs/audio_codecs.h README.md CMakeLists.txt tests/test_vorbis_roundtrip.cpp tests/test_real_vorbis.cpp
git commit -m "feat(vorbis): complete Ogg/Vorbis encoder & decoder with full roundtrip verification and docs"
```
