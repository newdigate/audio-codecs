#include "include/audio_codecs/aac/aac_encoder.h"
#include "include/audio_codecs/aac/aac_decoder.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

using namespace audio_codecs;
using namespace audio_codecs::aac;

// Helper to compute Signal-to-Noise Ratio (SNR) in dB
double calculate_snr(const float* ref, const float* test, size_t count) {
    double signal_energy = 0.0;
    double noise_energy = 0.0;

    for (size_t i = 0; i < count; ++i) {
        double s = ref[i];
        double err = ref[i] - test[i];
        signal_energy += s * s;
        noise_energy += err * err;
    }

    if (noise_energy < 1e-15) {
        return 100.0; // Perfect reconstruction
    }
    return 10.0 * std::log10(signal_energy / noise_energy);
}

void test_mono_sine_roundtrip() {
    std::cout << "Testing mono 440 Hz sine roundtrip (44.1 kHz, 64 kbps)...\n";
    AacEncoder encoder;
    AacDecoder decoder;

    AudioConfig config{44100, 1, 64, false, 4};
    assert(encoder.init(config));
    assert(decoder.init(config));

    constexpr size_t NUM_FRAMES = 12;
    constexpr size_t SAMPLES_PER_FRAME = 1024;
    constexpr size_t TOTAL_SAMPLES = NUM_FRAMES * SAMPLES_PER_FRAME;

    std::vector<float> in_pcm(TOTAL_SAMPLES);
    for (size_t i = 0; i < TOTAL_SAMPLES; ++i) {
        in_pcm[i] = 0.7f * std::sin(2.0 * constants::PI * 440.0 * i / 44100.0);
    }

    std::vector<float> decoded_pcm;
    uint8_t adts_packet[2048];
    float out_frame[1024];

    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const float* frame_ptr = &in_pcm[f * SAMPLES_PER_FRAME];
        int bytes = encoder.encode_frame(frame_ptr, SAMPLES_PER_FRAME, adts_packet, sizeof(adts_packet));
        assert(bytes > 0);

        int dec_samples = decoder.decode_frame(adts_packet, bytes, out_frame, 1024);
        assert(dec_samples == 1024);

        decoded_pcm.insert(decoded_pcm.end(), out_frame, out_frame + dec_samples);
    }

    size_t eval_start = 2 * SAMPLES_PER_FRAME;
    size_t eval_count = 8 * SAMPLES_PER_FRAME;

    double snr = calculate_snr(&in_pcm[eval_start], &decoded_pcm[eval_start + SAMPLES_PER_FRAME], eval_count);
    std::cout << "Mono 440 Hz Sine SNR: " << snr << " dB\n";
    assert(snr > 35.0);
}

void test_stereo_sine_roundtrip() {
    std::cout << "Testing stereo 440/880 Hz sine roundtrip (44.1 kHz, 128 kbps)...\n";
    AacEncoder encoder;
    AacDecoder decoder;

    AudioConfig config{44100, 2, 128, false, 4};
    assert(encoder.init(config));
    assert(decoder.init(config));

    constexpr size_t NUM_FRAMES = 12;
    constexpr size_t SAMPLES_PER_FRAME = 1024;
    constexpr size_t TOTAL_SAMPLES = NUM_FRAMES * SAMPLES_PER_FRAME * 2; // Interleaved stereo

    std::vector<float> in_pcm(TOTAL_SAMPLES);
    for (size_t i = 0; i < NUM_FRAMES * SAMPLES_PER_FRAME; ++i) {
        in_pcm[i * 2 + 0] = 0.6f * std::sin(2.0 * constants::PI * 440.0 * i / 44100.0);
        in_pcm[i * 2 + 1] = 0.6f * std::cos(2.0 * constants::PI * 880.0 * i / 44100.0);
    }

    std::vector<float> decoded_pcm;
    uint8_t adts_packet[4096];
    float out_frame[2048];

    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const float* frame_ptr = &in_pcm[f * SAMPLES_PER_FRAME * 2];
        int bytes = encoder.encode_frame(frame_ptr, SAMPLES_PER_FRAME * 2, adts_packet, sizeof(adts_packet));
        assert(bytes > 0);

        int dec_samples = decoder.decode_frame(adts_packet, bytes, out_frame, 2048);
        assert(dec_samples == 2048);

        decoded_pcm.insert(decoded_pcm.end(), out_frame, out_frame + dec_samples);
    }

    // Delay: 1024 samples per channel = 2048 interleaved samples
    size_t eval_start = 2 * SAMPLES_PER_FRAME * 2;
    size_t eval_count = 8 * SAMPLES_PER_FRAME * 2;

    double snr = calculate_snr(&in_pcm[eval_start], &decoded_pcm[eval_start + SAMPLES_PER_FRAME * 2], eval_count);
    std::cout << "Stereo 440/880 Hz Sine SNR: " << snr << " dB\n";
    assert(snr > 35.0);
}

void test_harmonic_chord_roundtrip() {
    std::cout << "Testing harmonic chord roundtrip (48 kHz, 192 kbps)...\n";
    AacEncoder encoder;
    AacDecoder decoder;

    AudioConfig config{48000, 2, 192, false, 4};
    assert(encoder.init(config));
    assert(decoder.init(config));

    constexpr size_t NUM_FRAMES = 12;
    constexpr size_t SAMPLES_PER_FRAME = 1024;
    constexpr size_t TOTAL_SAMPLES = NUM_FRAMES * SAMPLES_PER_FRAME * 2;

    std::vector<float> in_pcm(TOTAL_SAMPLES);
    for (size_t i = 0; i < NUM_FRAMES * SAMPLES_PER_FRAME; ++i) {
        float left = 0.25f * (std::sin(2.0 * constants::PI * 440.0 * i / 48000.0) +
                              std::sin(2.0 * constants::PI * 554.37 * i / 48000.0) +
                              std::sin(2.0 * constants::PI * 659.25 * i / 48000.0));
        float right = 0.25f * (std::sin(2.0 * constants::PI * 523.25 * i / 48000.0) +
                               std::sin(2.0 * constants::PI * 659.25 * i / 48000.0) +
                               std::sin(2.0 * constants::PI * 783.99 * i / 48000.0));
        in_pcm[i * 2 + 0] = left;
        in_pcm[i * 2 + 1] = right;
    }

    std::vector<float> decoded_pcm;
    uint8_t adts_packet[4096];
    float out_frame[2048];

    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const float* frame_ptr = &in_pcm[f * SAMPLES_PER_FRAME * 2];
        int bytes = encoder.encode_frame(frame_ptr, SAMPLES_PER_FRAME * 2, adts_packet, sizeof(adts_packet));
        assert(bytes > 0);

        int dec_samples = decoder.decode_frame(adts_packet, bytes, out_frame, 2048);
        assert(dec_samples == 2048);

        decoded_pcm.insert(decoded_pcm.end(), out_frame, out_frame + dec_samples);
    }

    size_t eval_start = 2 * SAMPLES_PER_FRAME * 2;
    size_t eval_count = 8 * SAMPLES_PER_FRAME * 2;

    double snr = calculate_snr(&in_pcm[eval_start], &decoded_pcm[eval_start + SAMPLES_PER_FRAME * 2], eval_count);
    std::cout << "Harmonic Chord SNR: " << snr << " dB\n";
    assert(snr > 35.0);
}

void test_frequency_sweep_roundtrip() {
    std::cout << "Testing logarithmic frequency sweep roundtrip (44.1 kHz, 128 kbps)...\n";
    AacEncoder encoder;
    AacDecoder decoder;

    AudioConfig config{44100, 1, 128, false, 4};
    assert(encoder.init(config));
    assert(decoder.init(config));

    constexpr size_t NUM_FRAMES = 16;
    constexpr size_t SAMPLES_PER_FRAME = 1024;
    constexpr size_t TOTAL_SAMPLES = NUM_FRAMES * SAMPLES_PER_FRAME;

    std::vector<float> in_pcm(TOTAL_SAMPLES);
    double f_start = 100.0;
    double f_end = 8000.0;
    double duration = static_cast<double>(TOTAL_SAMPLES) / 44100.0;

    for (size_t i = 0; i < TOTAL_SAMPLES; ++i) {
        double t = static_cast<double>(i) / 44100.0;
        double f = f_start * std::pow(f_end / f_start, t / duration);
        double phase = 2.0 * constants::PI * f_start * (std::pow(f_end / f_start, t / duration) - 1.0) / std::log(f_end / f_start) * duration;
        in_pcm[i] = 0.6f * static_cast<float>(std::sin(phase));
    }

    std::vector<float> decoded_pcm;
    uint8_t adts_packet[2048];
    float out_frame[1024];

    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const float* frame_ptr = &in_pcm[f * SAMPLES_PER_FRAME];
        int bytes = encoder.encode_frame(frame_ptr, SAMPLES_PER_FRAME, adts_packet, sizeof(adts_packet));
        assert(bytes > 0);

        int dec_samples = decoder.decode_frame(adts_packet, bytes, out_frame, 1024);
        assert(dec_samples == 1024);

        decoded_pcm.insert(decoded_pcm.end(), out_frame, out_frame + dec_samples);
    }

    size_t eval_start = 2 * SAMPLES_PER_FRAME;
    size_t eval_count = 12 * SAMPLES_PER_FRAME;

    double snr = calculate_snr(&in_pcm[eval_start], &decoded_pcm[eval_start + SAMPLES_PER_FRAME], eval_count);
    std::cout << "Log Sweep SNR: " << snr << " dB\n";
    assert(snr > 30.0);
}

void test_stereo_channel_separation() {
    std::cout << "Testing stereo channel separation (Left: 440 Hz, Right: 2500 Hz)...\n";
    AacEncoder encoder;
    AacDecoder decoder;

    AudioConfig config{44100, 2, 128, false, 4};
    assert(encoder.init(config));
    assert(decoder.init(config));

    constexpr size_t NUM_FRAMES = 8;
    constexpr size_t SAMPLES_PER_FRAME = 1024;
    constexpr size_t TOTAL_SAMPLES = NUM_FRAMES * SAMPLES_PER_FRAME * 2;

    std::vector<float> in_pcm(TOTAL_SAMPLES);
    for (size_t i = 0; i < NUM_FRAMES * SAMPLES_PER_FRAME; ++i) {
        in_pcm[i * 2 + 0] = 0.7f * std::sin(2.0 * constants::PI * 440.0 * i / 44100.0);
        in_pcm[i * 2 + 1] = 0.7f * std::sin(2.0 * constants::PI * 2500.0 * i / 44100.0);
    }

    std::vector<float> decoded_pcm;
    uint8_t adts_packet[4096];
    float out_frame[2048];

    for (size_t f = 0; f < NUM_FRAMES; ++f) {
        const float* frame_ptr = &in_pcm[f * SAMPLES_PER_FRAME * 2];
        int bytes = encoder.encode_frame(frame_ptr, SAMPLES_PER_FRAME * 2, adts_packet, sizeof(adts_packet));
        assert(bytes > 0);

        int dec_samples = decoder.decode_frame(adts_packet, bytes, out_frame, 2048);
        assert(dec_samples == 2048);

        decoded_pcm.insert(decoded_pcm.end(), out_frame, out_frame + dec_samples);
    }

    // Separate channels from steady-state section
    std::vector<float> left_ref, left_dec, right_ref, right_dec;
    size_t start_sample = 2 * SAMPLES_PER_FRAME;
    size_t end_sample = 6 * SAMPLES_PER_FRAME;

    for (size_t i = start_sample; i < end_sample; ++i) {
        left_ref.push_back(in_pcm[i * 2 + 0]);
        right_ref.push_back(in_pcm[i * 2 + 1]);

        size_t dec_i = i + SAMPLES_PER_FRAME;
        left_dec.push_back(decoded_pcm[dec_i * 2 + 0]);
        right_dec.push_back(decoded_pcm[dec_i * 2 + 1]);
    }

    double snr_left = calculate_snr(left_ref.data(), left_dec.data(), left_ref.size());
    double snr_right = calculate_snr(right_ref.data(), right_dec.data(), right_ref.size());

    std::cout << "Stereo Left Channel SNR: " << snr_left << " dB\n";
    std::cout << "Stereo Right Channel SNR: " << snr_right << " dB\n";

    assert(snr_left > 35.0);
    assert(snr_right > 35.0);
}

} // anonymous namespace

int main() {
    std::cout << "Starting AAC Encode-Decode Full Roundtrip tests...\n";
    test_mono_sine_roundtrip();
    test_stereo_sine_roundtrip();
    test_harmonic_chord_roundtrip();
    test_frequency_sweep_roundtrip();
    test_stereo_channel_separation();
    std::cout << "All AAC Roundtrip tests passed successfully!\n";
    return 0;
}
