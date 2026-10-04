#include <audio_codecs/audio_codecs.h>
#include <cassert>
#include <vector>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace audio_codecs::slice;

void test_end_to_end_synthetic_breakbeat() {
    // Generate 4 bars of 120 BPM drum break (4 bars * 2.0s = 8.0s @ 44.1k = 352800 samples)
    const uint32_t sample_rate = 44100;
    const uint32_t total_samples = sample_rate * 8;
    std::vector<int16_t> pcm(total_samples, 0);

    // Quarter note = 22050 samples.
    // Kick on beat 0 and 2 of each bar; Snare on beat 1 and 3 of each bar.
    for (int bar = 0; bar < 4; ++bar) {
        int bar_start = bar * (4 * 22050);
        // Kick beat 0
        pcm[bar_start + 0] = 28000; pcm[bar_start + 1] = -24000;
        // Snare beat 1
        pcm[bar_start + 22050] = 25000; pcm[bar_start + 22051] = -22000;
        // Kick beat 2
        pcm[bar_start + 44100] = 28000; pcm[bar_start + 44101] = -24000;
        // Snare beat 3
        pcm[bar_start + 66150] = 25000; pcm[bar_start + 66151] = -22000;
    }

    // Step 1: Generator in TRANSIENT_TO_GRID mode
    SliceGenerator generator;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_TRANSIENT_TO_GRID;
    cfg.sample_rate = sample_rate;
    cfg.bpm_q16 = 120 << 16;
    cfg.time_sig_num = 4;
    cfg.time_sig_denom = 4;
    bool ok = generator.init(cfg);
    assert(ok);
    (void)ok;

    // Stream PCM in 4096-sample blocks
    const uint32_t block_size = 4096;
    for (uint32_t offset = 0; offset < total_samples; offset += block_size) {
        uint32_t chunk = std::min(block_size, total_samples - offset);
        ok = generator.process_pcm(pcm.data() + offset, chunk);
        assert(ok);
    }

    ok = generator.finalize();
    assert(ok);
    assert(generator.is_complete());

    // 4 bars * 4 beats = 16 drum hits
    assert(generator.slice_count() >= 16);

    // Step 2: Serialize to memory buffer (.asl)
    uint8_t asl_buffer[4096];
    size_t bytes_written = 0;
    ok = generator.serialize(asl_buffer, sizeof(asl_buffer), &bytes_written);
    assert(ok);
    assert(bytes_written > sizeof(AslHeader));
    (void)bytes_written;

    // Step 3: Zero-heap reader queries
    SliceReader reader;
    ok = reader.init(asl_buffer, bytes_written);
    assert(ok);
    assert(reader.total_slices() == generator.slice_count());
    assert(reader.header().num_bars >= 4);

    // Verify slice lookups across all bars
    for (uint16_t i = 0; i < reader.total_slices(); ++i) {
        const AslSlice* s = reader.get_slice(i);
        assert(s != nullptr);
        assert(s->midi_note >= 36);
        (void)s;
    }

    // Step 4: Playback voice helper tempo stretch params
    const AslSlice* first_slice = reader.get_slice(0);
    assert(first_slice != nullptr);
    (void)first_slice;
    uint32_t trigger_interval = 0;
    uint32_t render_samples = 0;
    bool needs_tail = false;
    SliceVoiceHelper::compute_playback_params(*first_slice, 120 << 16, 100 << 16, sample_rate,
                                             &trigger_interval, &render_samples, &needs_tail);
    assert(needs_tail);
    assert(trigger_interval > first_slice->length_samples);
    (void)trigger_interval;
    (void)render_samples;
    (void)needs_tail;

    // Step 5: Export standard Type 0 MIDI file
    uint8_t midi_buf[2048];
    size_t midi_bytes = 0;
    ok = SliceMidiWriter::write_type0_midi(reader.header(), reader.get_slice(0),
                                          reader.total_slices(), midi_buf,
                                          sizeof(midi_buf), &midi_bytes);
    assert(ok);
    assert(midi_bytes > 64);
    assert(std::memcmp(midi_buf, "MThd", 4) == 0);
    (void)midi_bytes;
}

int main() {
    test_end_to_end_synthetic_breakbeat();
    std::cout << "test_slice_integration PASSED\n";
    return 0;
}
