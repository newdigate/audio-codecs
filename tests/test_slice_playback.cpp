#include <audio_codecs/slice/slice_playback.h>
#include <audio_codecs/slice/slice_midi_writer.h>
#include <cassert>
#include <vector>
#include <cstring>
#include <iostream>

using namespace audio_codecs::slice;

void test_playback_tempo_math() {
    AslSlice slice{};
    slice.start_sample = 0;
    slice.length_samples = 22050; // 0.5s at 44.1k (quarter note at 120 BPM)
    slice.musical_tick = 0;

    uint32_t trigger_interval = 0;
    uint32_t render_samples = 0;
    bool needs_tail = false;

    // Faster tempo: 140 BPM
    SliceVoiceHelper::compute_playback_params(slice, 120 << 16, 140 << 16, 44100,
                                             &trigger_interval, &render_samples, &needs_tail);
    assert(trigger_interval < 22050);
    assert(!needs_tail);
    (void)trigger_interval;
    (void)render_samples;
    (void)needs_tail;

    // Slower tempo: 100 BPM
    SliceVoiceHelper::compute_playback_params(slice, 120 << 16, 100 << 16, 44100,
                                             &trigger_interval, &render_samples, &needs_tail);
    assert(trigger_interval > 22050);
    assert(needs_tail);
    (void)trigger_interval;
    (void)render_samples;
    (void)needs_tail;

    // Equal tempo: 120 BPM
    SliceVoiceHelper::compute_playback_params(slice, 120 << 16, 120 << 16, 44100,
                                             &trigger_interval, &render_samples, &needs_tail);
    assert(trigger_interval == 22050);
    assert(render_samples == 22050);
    assert(!needs_tail);

    // Nullptr safety
    SliceVoiceHelper::compute_playback_params(slice, 120 << 16, 120 << 16, 44100,
                                             nullptr, nullptr, nullptr);

    // Default 0 BPM fallback
    SliceVoiceHelper::compute_playback_params(slice, 0, 0, 44100,
                                             &trigger_interval, &render_samples, &needs_tail);
    assert(trigger_interval == 22050);
    assert(!needs_tail);
}

void test_micro_fade_curves() {
    std::vector<int16_t> pcm(128, 10000);

    // Attack ramp: first sample should be attenuated near 0
    SliceVoiceHelper::apply_attack_ramp(pcm.data(), static_cast<uint32_t>(pcm.size()), 44100);
    assert(pcm[0] == 0);
    assert(pcm[127] == 10000);

    // Choke ramp: end sample should be attenuated near 0
    SliceVoiceHelper::apply_choke_ramp(pcm.data(), static_cast<uint32_t>(pcm.size()), 44100);
    assert(pcm[127] == 0);

    // Edge cases: null pointer, zero count, zero sample rate
    SliceVoiceHelper::apply_attack_ramp(nullptr, 128, 44100);
    SliceVoiceHelper::apply_attack_ramp(pcm.data(), 0, 44100);
    SliceVoiceHelper::apply_attack_ramp(pcm.data(), 128, 0);

    SliceVoiceHelper::apply_choke_ramp(nullptr, 128, 44100);
    SliceVoiceHelper::apply_choke_ramp(pcm.data(), 0, 44100);
    SliceVoiceHelper::apply_choke_ramp(pcm.data(), 128, 0);

    // Small buffer: count less than ramp_len
    std::vector<int16_t> small_pcm(8, 10000);
    SliceVoiceHelper::apply_attack_ramp(small_pcm.data(), 8, 44100);
    assert(small_pcm[0] == 0);

    std::vector<int16_t> small_pcm2(8, 10000);
    SliceVoiceHelper::apply_choke_ramp(small_pcm2.data(), 8, 44100);
    assert(small_pcm2[7] == 0);
}

void test_midi_type0_export() {
    AslHeader hdr{};
    hdr.magic = ASL_MAGIC;
    hdr.version = ASL_VERSION;
    hdr.header_size = 128;
    hdr.time_signature_num = 4;
    hdr.time_signature_denom = 4;
    hdr.original_bpm_q16 = 120 << 16;
    hdr.ppqn = 480;
    hdr.total_slices = 2;

    AslSlice slices[2]{};
    slices[0].start_sample = 0;
    slices[0].length_samples = 22050;
    slices[0].musical_tick = 0;
    slices[0].midi_note = 36;

    slices[1].start_sample = 22050;
    slices[1].length_samples = 22050;
    slices[1].musical_tick = 480;
    slices[1].midi_note = 37;

    uint8_t midi_buf[512];
    size_t written = 0;
    bool ok = SliceMidiWriter::write_type0_midi(hdr, slices, 2, midi_buf, sizeof(midi_buf), &written);
    assert(ok);
    assert(written > 32);
    (void)ok;
    (void)written;

    // Verify MIDI Header: 'MThd', header size 6, format 0, 1 track, 480 PPQN
    assert(std::memcmp(midi_buf, "MThd", 4) == 0);
    assert(midi_buf[4] == 0 && midi_buf[5] == 0 && midi_buf[6] == 0 && midi_buf[7] == 6);
    assert(midi_buf[8] == 0 && midi_buf[9] == 0); // Format 0
    assert(midi_buf[10] == 0 && midi_buf[11] == 1); // 1 track
    uint16_t ppqn = (static_cast<uint16_t>(midi_buf[12]) << 8) | midi_buf[13];
    assert(ppqn == 480);
    (void)ppqn;

    // Verify Track Chunk: 'MTrk'
    assert(std::memcmp(midi_buf + 14, "MTrk", 4) == 0);
    uint32_t track_len = (static_cast<uint32_t>(midi_buf[18]) << 24) |
                         (static_cast<uint32_t>(midi_buf[19]) << 16) |
                         (static_cast<uint32_t>(midi_buf[20]) << 8) |
                         static_cast<uint32_t>(midi_buf[21]);
    assert(written == 14 + 8 + track_len);
    (void)track_len;

    // Verify End of Track event (0x00 0xFF 0x2F 0x00) at the end of track
    assert(midi_buf[written - 4] == 0x00);
    assert(midi_buf[written - 3] == 0xFF);
    assert(midi_buf[written - 2] == 0x2F);
    assert(midi_buf[written - 1] == 0x00);

    // Error handling checks
    size_t dummy_written = 0;
    assert(!SliceMidiWriter::write_type0_midi(hdr, nullptr, 2, midi_buf, sizeof(midi_buf), &dummy_written));
    assert(!SliceMidiWriter::write_type0_midi(hdr, slices, 0, midi_buf, sizeof(midi_buf), &dummy_written));
    assert(!SliceMidiWriter::write_type0_midi(hdr, slices, 2, nullptr, sizeof(midi_buf), &dummy_written));
    assert(!SliceMidiWriter::write_type0_midi(hdr, slices, 2, midi_buf, 64, &dummy_written));
    (void)dummy_written;
}

int main() {
    test_playback_tempo_math();
    test_micro_fade_curves();
    test_midi_type0_export();
    std::cout << "test_slice_playback PASSED\n";
    return 0;
}
