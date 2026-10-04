#include <audio_codecs/slice/slice_detector.h>
#include <cassert>
#include <cmath>
#include <vector>
#include <iostream>

using namespace audio_codecs::slice;

void test_zero_crossing_backward() {
    // Generate a sine wave that crosses zero at index 50
    std::vector<int16_t> pcm(128, 0);
    for (int i = 0; i < 128; ++i) {
        // Zero at i = 50
        double val = std::sin((i - 50) * 0.1) * 10000.0;
        pcm[i] = static_cast<int16_t>(val);
    }
    // Peak is at around index 65
    uint32_t peak = 65;
    uint32_t zc = SliceDetector::find_zero_crossing_backward(pcm.data(), peak, 32);
    assert(zc == 50 || zc == 51);
    (void)zc;

    // Boundary edge cases
    uint32_t zc_null = SliceDetector::find_zero_crossing_backward(nullptr, 10, 10);
    assert(zc_null == 10);
    (void)zc_null;

    uint32_t zc_zero = SliceDetector::find_zero_crossing_backward(pcm.data(), 0, 10);
    assert(zc_zero == 0);
    (void)zc_zero;
}

void test_zero_crossing_no_crossing() {
    // Monotonic positive signal with no zero crossing in search window
    std::vector<int16_t> pcm(64, 5000);
    uint32_t zc = SliceDetector::find_zero_crossing_backward(pcm.data(), 30, 10);
    assert(zc == 30);
    (void)zc;
}

void test_detector_impulse_detection() {
    SliceDetector detector;
    DetectorConfig cfg;
    cfg.sample_rate = 44100;
    cfg.sensitivity = 0.5f;
    cfg.min_slice_samples = 512;
    cfg.pre_emphasis_alpha = 0.95f;
    bool ok = detector.init(cfg);
    assert(ok);
    (void)ok;

    // Create 44100 samples (1 sec) with 2 synthetic impulses at sample 4410 (0.1s) and 22050 (0.5s)
    std::vector<int16_t> pcm(44100, 0);
    // Impulse 1 at 4410
    pcm[4410] = 20000;
    pcm[4411] = 15000;
    pcm[4412] = 10000;
    pcm[4413] = -5000;

    // Impulse 2 at 22050
    pcm[22050] = 25000;
    pcm[22051] = 18000;
    pcm[22052] = 8000;
    pcm[22053] = -6000;

    uint32_t detected[16];
    uint32_t count = detector.process_block(pcm.data(), static_cast<uint32_t>(pcm.size()), detected, 16);
    
    // We should detect exactly 2 transients
    assert(count == 2);
    (void)count;
    // Transient 1 should be snapped close to 4410
    assert(detected[0] >= 4380 && detected[0] <= 4412);
    // Transient 2 should be snapped close to 22050
    assert(detected[1] >= 22020 && detected[1] <= 22052);
    (void)detected;
}

void test_detector_silence_rejection() {
    SliceDetector detector;
    DetectorConfig cfg;
    bool ok = detector.init(cfg);
    assert(ok);
    (void)ok;

    std::vector<int16_t> silence(4410, 0);
    uint32_t detected[16];
    uint32_t count = detector.process_block(silence.data(), static_cast<uint32_t>(silence.size()), detected, 16);
    assert(count == 0);
    (void)count;
    (void)detected;

    // Null buffer / zero count edge cases without function calls inside assert()
    uint32_t c1 = detector.process_block(nullptr, 100, detected, 16);
    assert(c1 == 0);
    (void)c1;

    uint32_t c2 = detector.process_block(silence.data(), 0, detected, 16);
    assert(c2 == 0);
    (void)c2;

    uint32_t c3 = detector.process_block(silence.data(), 100, nullptr, 16);
    assert(c3 == 0);
    (void)c3;

    uint32_t c4 = detector.process_block(silence.data(), 100, detected, 0);
    assert(c4 == 0);
    (void)c4;
}

void test_max_indices_capping() {
    SliceDetector detector;
    DetectorConfig cfg;
    cfg.min_slice_samples = 32;
    bool ok = detector.init(cfg);
    assert(ok);
    (void)ok;

    // Buffer with 3 impulses, max_indices = 2
    std::vector<int16_t> pcm(4410, 0);
    pcm[320] = 20000;
    pcm[640] = 20000;
    pcm[960] = 20000;

    uint32_t detected[2];
    uint32_t count = detector.process_block(pcm.data(), static_cast<uint32_t>(pcm.size()), detected, 2);
    assert(count == 2);
    (void)count;
    (void)detected;
}

void test_detector_ram_size() {
    static_assert(sizeof(SliceDetector) < 200, "SliceDetector RAM working state must be < 200 bytes");
}

int main() {
    test_zero_crossing_backward();
    test_zero_crossing_no_crossing();
    test_detector_impulse_detection();
    test_detector_silence_rejection();
    test_max_indices_capping();
    test_detector_ram_size();
    std::cout << "test_slice_detector PASSED\n";
    return 0;
}
