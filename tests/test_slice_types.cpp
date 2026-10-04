#include <audio_codecs/slice/slice_types.h>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace audio_codecs::slice;

void test_sizes_and_alignments() {
    static_assert(sizeof(AslHeader) == 128, "AslHeader size must be exactly 128 bytes");
    static_assert(sizeof(AslSlice) == 32, "AslSlice size must be exactly 32 bytes");

    size_t s_hdr = sizeof(AslHeader);
    size_t s_slice = sizeof(AslSlice);
    assert(s_hdr == 128);
    assert(s_slice == 32);
    (void)s_hdr;
    (void)s_slice;
}

void test_header_defaults_and_roundtrip() {
    AslHeader hdr{};
    hdr.magic = ASL_MAGIC;
    hdr.version = ASL_VERSION;
    hdr.header_size = 128;
    hdr.duration_samples = 44100 * 4;
    hdr.duration_ms = 4000;
    hdr.sample_rate = 44100;
    hdr.channels = 2;
    hdr.time_signature_num = 4;
    hdr.time_signature_denom = 4;
    hdr.slice_mode = ASL_MODE_TRANSIENT_TO_GRID;
    hdr.original_bpm_q16 = (120 << 16);
    hdr.ppqn = ASL_PPQN;
    hdr.num_bars = 2;
    hdr.total_slices = 16;
    hdr.slice_descriptor_size = sizeof(AslSlice);
    hdr.slices_offset = 128;
    hdr.flags = ASL_FLAG_LOOPABLE;

    uint8_t buffer[128];
    std::memcpy(buffer, &hdr, sizeof(hdr));

    AslHeader read_hdr{};
    std::memcpy(&read_hdr, buffer, sizeof(read_hdr));

    int memcmp_hdr = std::memcmp(&hdr, &read_hdr, sizeof(AslHeader));
    assert(memcmp_hdr == 0);
    (void)memcmp_hdr;

    assert(read_hdr.magic == ASL_MAGIC);
    assert(read_hdr.version == ASL_VERSION);
    assert(read_hdr.header_size == 128);
    assert(read_hdr.duration_samples == 44100 * 4);
    assert(read_hdr.duration_ms == 4000);
    assert(read_hdr.sample_rate == 44100);
    assert(read_hdr.channels == 2);
    assert(read_hdr.time_signature_num == 4);
    assert(read_hdr.time_signature_denom == 4);
    assert(read_hdr.slice_mode == ASL_MODE_TRANSIENT_TO_GRID);
    assert(read_hdr.original_bpm_q16 == (120 << 16));
    assert(read_hdr.ppqn == 480);
    assert(read_hdr.num_bars == 2);
    assert(read_hdr.total_slices == 16);
    assert(read_hdr.slice_descriptor_size == 32);
    assert(read_hdr.slices_offset == 128);
    assert(read_hdr.flags == ASL_FLAG_LOOPABLE);
}

void test_slice_descriptor_fields() {
    AslSlice slice{};
    slice.start_sample = 22050;
    slice.length_samples = 11025;
    slice.musical_tick = 240;
    slice.midi_note = 36;
    slice.bar_index = 0;
    slice.beat_within_bar = 0;
    slice.subdivision = 2;
    slice.gain_db_q8 = 0;
    slice.transient_energy = 32000;
    slice.decay_ms = 250;
    slice.tail_mode = ASL_TAIL_STRETCH_DECAY;
    slice.flags = ASL_SLICE_FLAG_LOCKED;

    uint8_t buffer[32];
    std::memcpy(buffer, &slice, sizeof(slice));

    AslSlice read_slice{};
    std::memcpy(&read_slice, buffer, sizeof(read_slice));

    int memcmp_slice = std::memcmp(&slice, &read_slice, sizeof(AslSlice));
    assert(memcmp_slice == 0);
    (void)memcmp_slice;

    assert(read_slice.start_sample == 22050);
    assert(read_slice.length_samples == 11025);
    assert(read_slice.musical_tick == 240);
    assert(read_slice.midi_note == 36);
    assert(read_slice.bar_index == 0);
    assert(read_slice.beat_within_bar == 0);
    assert(read_slice.subdivision == 2);
    assert(read_slice.gain_db_q8 == 0);
    assert(read_slice.transient_energy == 32000);
    assert(read_slice.decay_ms == 250);
    assert(read_slice.tail_mode == ASL_TAIL_STRETCH_DECAY);
    assert(read_slice.flags == ASL_SLICE_FLAG_LOCKED);
}

int main() {
    test_sizes_and_alignments();
    test_header_defaults_and_roundtrip();
    test_slice_descriptor_fields();
    std::cout << "test_slice_types PASSED\n";
    return 0;
}
