#include <audio_codecs/slice/slice_generator.h>
#include <cassert>
#include <vector>
#include <iostream>

using namespace audio_codecs::slice;

void test_pure_transient_mode() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_TRANSIENT;
    cfg.sample_rate = 44100;
    cfg.bpm_q16 = 120 << 16;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    // 2-second audio (88200 samples) with transients at 0.25s, 0.5s, 1.0s
    std::vector<int16_t> pcm(88200, 0);
    pcm[11025] = 25000; pcm[11026] = -20000;
    pcm[22050] = 25000; pcm[22051] = -20000;
    pcm[44100] = 25000; pcm[44101] = -20000;

    ok = gen.process_pcm(pcm.data(), static_cast<uint32_t>(pcm.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    assert(gen.slice_count() >= 3);
    const AslSlice* s0 = gen.get_slice(0);
    assert(s0 != nullptr);
    assert(s0->start_sample <= 11030);
    assert(s0->midi_note == 36); // First slice is C1
    (void)s0;
}

void test_metric_grid_mode() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_GRID;
    cfg.sample_rate = 44100;
    cfg.bpm_q16 = 120 << 16; // 120 BPM -> 0.5s per quarter note, 0.125s per 16th note (5512.5 samples)
    cfg.time_sig_num = 4;
    cfg.time_sig_denom = 4;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    // 1 bar of silence = 2 seconds = 88200 samples
    std::vector<int16_t> silence(88200, 0);
    ok = gen.process_pcm(silence.data(), static_cast<uint32_t>(silence.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    // 1 bar of 4/4 has 16 sixteenth-notes
    assert(gen.slice_count() == 16);
    for (uint16_t i = 0; i < 16; ++i) {
        const AslSlice* s = gen.get_slice(i);
        assert(s != nullptr);
        assert(s->bar_index == 0);
        assert(s->beat_within_bar == (i / 4));
        assert(s->subdivision == (i % 4));
        assert(s->musical_tick == i * 120); // 120 ticks per 16th note at PPQN=480
        assert(s->midi_note == static_cast<uint8_t>(36 + i));
        (void)s;
    }
}

void test_transient_to_grid_quantization() {
    SliceGenerator gen;
    GeneratorConfig cfg;
    cfg.mode = ASL_MODE_TRANSIENT_TO_GRID;
    cfg.sample_rate = 44100;
    cfg.bpm_q16 = 120 << 16;
    cfg.time_sig_num = 4;
    cfg.time_sig_denom = 4;
    bool ok = gen.init(cfg);
    assert(ok);
    (void)ok;

    // 1 bar = 88200 samples. Kick at 0, Snare at beat 2 (44100 samples) with slight human jitter (+100 samples)
    std::vector<int16_t> pcm(88200, 0);
    pcm[0] = 20000; pcm[1] = -15000;
    pcm[44200] = 25000; pcm[44201] = -20000;

    ok = gen.process_pcm(pcm.data(), static_cast<uint32_t>(pcm.size()));
    assert(ok);
    ok = gen.finalize();
    assert(ok);

    assert(gen.slice_count() >= 2);
    const AslSlice* s0 = gen.get_slice(0);
    assert(s0 != nullptr);
    assert(s0->musical_tick == 0);
    assert(s0->bar_index == 0);
    assert(s0->beat_within_bar == 0);
    (void)s0;

    // Find the slice corresponding to beat 2 (snare)
    const AslSlice* s_snare = nullptr;
    for (uint16_t i = 0; i < gen.slice_count(); ++i) {
        const AslSlice* s = gen.get_slice(i);
        if (s->start_sample >= 44100 && s->start_sample <= 44300) {
            s_snare = s;
            break;
        }
    }
    assert(s_snare != nullptr);
    // Snapped to beat 2: 2 beats * 480 = 960 ticks
    assert(s_snare->musical_tick == 960);
    assert(s_snare->beat_within_bar == 2);
    (void)s_snare;
}

int main() {
    test_pure_transient_mode();
    test_metric_grid_mode();
    test_transient_to_grid_quantization();
    std::cout << "test_slice_modes PASSED\n";
    return 0;
}
