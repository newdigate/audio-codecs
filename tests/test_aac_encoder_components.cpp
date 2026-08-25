#include "src/aac/encoder/transient_detector.h"
#include "src/aac/encoder/psychoacoustic.h"
#include "src/aac/encoder/quantizer.h"
#include "src/aac/encoder/huffman_encoder.h"
#include "src/aac/decoder/huffman_decoder.h"
#include "src/aac/decoder/requantizer.h"
#include "src/aac/aac_tables.h"
#include "src/core/bit_reader.h"
#include "src/core/bit_writer.h"
#include "src/core/math_constants.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

void test_transient_detector() {
    using namespace audio_codecs::aac;

    TransientDetector detector;
    assert(detector.get_current_sequence() == WindowSequence::OnlyLong);

    // 1. Steady sine wave: should remain OnlyLong
    std::vector<float> sine_wave(AAC_WINDOW_LEN_LONG, 0.0f);
    for (size_t i = 0; i < AAC_WINDOW_LEN_LONG; ++i) {
        sine_wave[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
    }

    WindowSequence seq1 = detector.update(sine_wave.data());
    assert(seq1 == WindowSequence::OnlyLong);

    WindowSequence seq2 = detector.update(sine_wave.data());
    assert(seq2 == WindowSequence::OnlyLong);

    // 2. Strong impulse/attack in sub-block 4 (around sample 1024)
    std::vector<float> transient_wave(AAC_WINDOW_LEN_LONG, 0.0f);
    // Silence before sample 1024, high amplitude sharp pulse after sample 1024
    for (size_t i = 0; i < 1024; ++i) {
        transient_wave[i] = 0.001f * std::sin(2.0f * 3.14159265f * 440.0f * i / 44100.0f);
    }
    for (size_t i = 1024; i < AAC_WINDOW_LEN_LONG; ++i) {
        transient_wave[i] = 0.95f * std::sin(2.0f * 3.14159265f * 3000.0f * i / 44100.0f);
    }

    WindowSequence seq_trans = detector.update(transient_wave.data());
    assert(seq_trans == WindowSequence::LongStart);

    // Next sequence must transition to EightShort
    WindowSequence seq_short = detector.update(sine_wave.data());
    assert(seq_short == WindowSequence::EightShort);

    // Next sequence after EightShort with steady signal should transition to LongStop
    WindowSequence seq_stop = detector.update(sine_wave.data());
    assert(seq_stop == WindowSequence::LongStop);

    // Next sequence after LongStop should return to OnlyLong
    WindowSequence seq_long = detector.update(sine_wave.data());
    assert(seq_long == WindowSequence::OnlyLong);
}

void test_psychoacoustic_model() {
    using namespace audio_codecs::aac;

    PsychoacousticModel model;
    model.init(44100);

    size_t num_swb = 0;
    const int* swb = get_swb_offset_long(44100, num_swb);
    assert(swb != nullptr);
    assert(num_swb > 0);

    std::vector<float> pcm(AAC_WINDOW_LEN_LONG, 0.0f);
    // 1 kHz sine wave
    for (size_t i = 0; i < AAC_WINDOW_LEN_LONG; ++i) {
        pcm[i] = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0f);
    }

    std::vector<float> thresholds(num_swb, 0.0f);
    std::vector<float> energy(num_swb, 0.0f);
    float pe = 0.0f;

    model.analyze_long(pcm.data(), 44100, thresholds.data(), energy.data(), pe);

    // Verify positive energy and thresholds
    float total_energy = 0.0f;
    for (size_t b = 0; b < num_swb; ++b) {
        assert(thresholds[b] > 0.0f);
        assert(energy[b] >= 0.0f);
        total_energy += energy[b];
    }
    assert(total_energy > 1.0f);
    assert(pe > 0.0f);

    // 1 kHz at 44.1 kHz sample rate lands near bin 1000 * 2048 / 44100 ~ 46 (band ~9-10)
    // Energy should peak around band 8-11
    float max_e = 0.0f;
    size_t max_b = 0;
    for (size_t b = 0; b < num_swb; ++b) {
        if (energy[b] > max_e) {
            max_e = energy[b];
            max_b = b;
        }
    }
    assert(max_b >= 7 && max_b <= 13);
    assert(thresholds[max_b] > thresholds[0]); // Masking threshold rises around the tone

    // Test short window psychoacoustic analysis
    size_t num_swb_short = 0;
    const int* swb_short = get_swb_offset_short(44100, num_swb_short);
    assert(swb_short != nullptr);

    std::vector<float> pcm_short(AAC_WINDOW_LEN_SHORT, 0.0f);
    for (size_t i = 0; i < AAC_WINDOW_LEN_SHORT; ++i) {
        pcm_short[i] = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 44100.0f);
    }
    std::vector<float> thresholds_short(num_swb_short, 0.0f);
    std::vector<float> energy_short(num_swb_short, 0.0f);
    float pe_short = 0.0f;

    model.analyze_short(pcm_short.data(), 44100, thresholds_short.data(), energy_short.data(), pe_short);
    assert(pe_short > 0.0f);
    for (size_t b = 0; b < num_swb_short; ++b) {
        assert(thresholds_short[b] > 0.0f);
    }
}

void test_quantizer_roundtrip_accuracy() {
    using namespace audio_codecs::aac;

    size_t num_swb = 0;
    const int* swb = get_swb_offset_long(44100, num_swb);

    std::vector<float> in_spec(1024, 0.0f);
    std::vector<float> mask_thr(num_swb, 0.01f);

    // Create synthetic spectral peaks
    for (size_t b = 0; b < num_swb; ++b) {
        int start = swb[b];
        int end = swb[b + 1];
        for (int i = start; i < end; ++i) {
            if ((i - start) % 4 == 0) {
                in_spec[i] = 50.0f / (1.0f + 0.1f * b);
            } else if ((i - start) % 4 == 2) {
                in_spec[i] = -30.0f / (1.0f + 0.1f * b);
            }
        }
    }

    std::vector<int> out_quant(1024, 0);
    std::vector<int> out_sf(num_swb, 0);
    int global_gain = 0;

    AacQuantizer quantizer;
    quantizer.quantize_spectrum_fast(in_spec.data(), mask_thr.data(), swb, num_swb,
                                    out_quant.data(), out_sf.data(), global_gain, 4000);

    assert(global_gain > 0 && global_gain < 255);

    // Dequantize with requantize_spectrum
    std::vector<float> reconstructed(1024, 0.0f);
    requantize_spectrum(out_quant.data(), out_sf.data(), swb, num_swb, reconstructed.data());

    // Check SNR / reconstruction error
    float sig_pow = 0.0f;
    float err_pow = 0.0f;
    for (size_t i = 0; i < 1024; ++i) {
        sig_pow += in_spec[i] * in_spec[i];
        float diff = in_spec[i] - reconstructed[i];
        err_pow += diff * diff;
    }

    float snr = 10.0f * std::log10(sig_pow / (err_pow + 1e-12f));
    std::cout << "Quantizer SNR: " << snr << " dB\n";
    assert(snr > 15.0f); // Fast MCU quantizer delivers > 15 dB SNR on active spectrum
}

void test_huffman_encoder_codebook_selection_and_packing() {
    using namespace audio_codecs::aac;
    using namespace audio_codecs::core;

    size_t num_swb = 0;
    const int* swb = get_swb_offset_long(44100, num_swb);

    std::vector<int> quant_spec(1024, 0);
    std::vector<int> sf(num_swb, 100);

    // Band 0: all zeros -> should pick HCB_ZERO (0)
    // Band 1: small values {-1, 0, 1} -> CB 1 or 2
    for (int i = swb[1]; i < swb[2]; i += 4) {
        quant_spec[i + 0] = 1;
        quant_spec[i + 1] = 0;
        quant_spec[i + 2] = -1;
        quant_spec[i + 3] = 1;
    }
    // Band 2: medium values {-3, 4, -2, 1} -> CB 5 or 6
    for (int i = swb[2]; i < swb[3]; i += 2) {
        quant_spec[i + 0] = 3;
        quant_spec[i + 1] = -4;
    }
    // Band 3: larger values {-10, 12} -> CB 9 or 10
    for (int i = swb[3]; i < swb[4]; i += 2) {
        quant_spec[i + 0] = -10;
        quant_spec[i + 1] = 12;
    }
    // Band 4: large values {25, -150} -> CB 11 (ESC)
    for (int i = swb[4]; i < swb[5]; i += 2) {
        quant_spec[i + 0] = 25;
        quant_spec[i + 1] = -150;
    }

    std::vector<int> band_cb(num_swb, 0);
    for (size_t b = 0; b < num_swb; ++b) {
        int start = swb[b];
        int width = swb[b + 1] - start;
        band_cb[b] = AacHuffmanEncoder::find_best_codebook_for_band(&quant_spec[start], width);
    }

    assert(band_cb[0] == HCB_ZERO);
    assert(band_cb[1] == 1 || band_cb[1] == 2 || band_cb[1] == 3);
    assert(band_cb[2] >= 5 && band_cb[2] <= 6);
    assert(band_cb[3] >= 9 && band_cb[3] <= 10);
    assert(band_cb[4] == HCB_ESC);

    // Build sections
    std::vector<int> sect_cb(num_swb, 0);
    std::vector<int> sect_len(num_swb, 0);
    size_t num_sections = 0;
    AacHuffmanEncoder::build_sections(band_cb.data(), num_swb, sect_cb.data(), sect_len.data(), num_sections);
    assert(num_sections > 0 && num_sections <= num_swb);

    // Pack into BitWriter
    std::vector<uint8_t> buffer(4096, 0);
    BitWriter writer;
    writer.init(buffer.data(), buffer.size());

    int global_gain = 100;
    size_t sec_bits = AacHuffmanEncoder::write_section_data(writer, sect_cb.data(), sect_len.data(), num_sections);
    size_t sf_bits  = AacHuffmanEncoder::write_scalefactor_data(writer, global_gain, sf.data(), band_cb.data(), num_swb);
    size_t spec_bits = AacHuffmanEncoder::write_spectral_data(writer, quant_spec.data(), band_cb.data(), swb, num_swb);

    assert(sec_bits > 0);
    assert(sf_bits > 0);
    assert(spec_bits > 0);
    writer.flush_to_byte();

    // Verify bitstream decoding matches original quant_spec
    BitReader reader;
    reader.init(buffer.data(), writer.get_byte_count());

    // 1. Decode Section Data
    std::vector<int> decoded_band_cb(num_swb, 0);
    size_t decoded_sfb = 0;
    for (size_t s = 0; s < num_sections; ++s) {
        assert(reader.bits_remaining() >= 4);
        uint8_t scb = static_cast<uint8_t>(reader.read_bits(4));
        uint8_t slen = 0;
        uint8_t incr = static_cast<uint8_t>(reader.read_bits(5));
        while (incr == 31) {
            slen += 31;
            incr = static_cast<uint8_t>(reader.read_bits(5));
        }
        slen += incr;
        for (size_t b = decoded_sfb; b < decoded_sfb + slen && b < num_swb; ++b) {
            decoded_band_cb[b] = scb;
        }
        decoded_sfb += slen;
    }
    for (size_t b = 0; b < num_swb; ++b) {
        assert(decoded_band_cb[b] == band_cb[b]);
    }

    // 2. Decode Scalefactor Data
    int last_sf = global_gain;
    std::vector<int> decoded_sf(num_swb, 0);
    for (size_t b = 0; b < num_swb; ++b) {
        if (decoded_band_cb[b] == HCB_ZERO) {
            decoded_sf[b] = 0;
        } else {
            int delta = 0;
            bool ok = decode_scalefactor_delta(reader, delta);
            assert(ok);
            last_sf += delta;
            decoded_sf[b] = last_sf;
        }
    }

    // 3. Decode Spectral Data
    std::vector<int> decoded_quant(1024, 0);
    for (size_t b = 0; b < num_swb; ++b) {
        int cb = decoded_band_cb[b];
        int start = swb[b];
        int width = swb[b + 1] - start;
        bool ok = decode_spectral_data(reader, cb, &decoded_quant[start], width);
        assert(ok);
    }

    // Compare original quant_spec with decoded_quant
    for (size_t i = 0; i < 1024; ++i) {
        assert(decoded_quant[i] == quant_spec[i]);
    }
}

int main() {
    std::cout << "Testing AAC Encoder Components (Transient Detector, Psychoacoustics, Quantizer, Huffman Encoder)...\n";
    test_transient_detector();
    test_psychoacoustic_model();
    test_quantizer_roundtrip_accuracy();
    test_huffman_encoder_codebook_selection_and_packing();
    std::cout << "All AAC Encoder Component tests passed successfully!\n";
    return 0;
}
