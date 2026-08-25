#include "src/aac/decoder/stereo_processor.h"
#include "src/aac/decoder/tns_decoder.h"
#include "include/audio_codecs/aac/aac_types.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <numeric>

int main() {
    using namespace audio_codecs::aac;

    std::cout << "Starting AAC Stereo and TNS tests...\n";

    // -------------------------------------------------------------
    // Test 1: M/S Stereo (Long Window)
    // -------------------------------------------------------------
    {
        std::cout << "Testing M/S Stereo (Long window)...\n";
        constexpr size_t num_swb = 3;
        int swb_offsets[num_swb + 1] = {0, 4, 8, 12};
        float left[12]  = {3.0f, 3.0f, 3.0f, 3.0f,  10.0f, 10.0f, 10.0f, 10.0f,  0.5f,  0.5f,  0.5f,  0.5f};
        float right[12] = {1.0f, 1.0f, 1.0f, 1.0f,   2.0f,  2.0f,  2.0f,  2.0f, -0.5f, -0.5f, -0.5f, -0.5f};
        uint8_t ms_used[num_swb] = {1, 0, 1}; // Band 0 & 2 active, Band 1 inactive

        apply_ms_stereo(left, right, ms_used, swb_offsets, num_swb);

        // Band 0: ms_used == 1 -> L = 3+1 = 4, R = 3-1 = 2
        for (int i = 0; i < 4; ++i) {
            assert(std::fabs(left[i] - 4.0f) < 1e-5f);
            assert(std::fabs(right[i] - 2.0f) < 1e-5f);
        }

        // Band 1: ms_used == 0 -> L = 10, R = 2 (unchanged)
        for (int i = 4; i < 8; ++i) {
            assert(std::fabs(left[i] - 10.0f) < 1e-5f);
            assert(std::fabs(right[i] - 2.0f) < 1e-5f);
        }

        // Band 2: ms_used == 1 -> L = 0.5 + (-0.5) = 0.0, R = 0.5 - (-0.5) = 1.0
        for (int i = 8; i < 12; ++i) {
            assert(std::fabs(left[i] - 0.0f) < 1e-5f);
            assert(std::fabs(right[i] - 1.0f) < 1e-5f);
        }
    }

    // -------------------------------------------------------------
    // Test 2: M/S Stereo (Short Windows)
    // -------------------------------------------------------------
    {
        std::cout << "Testing M/S Stereo (Short windows)...\n";
        constexpr size_t num_swb = 2;
        constexpr size_t num_windows = 8;
        int swb_offsets[num_swb + 1] = {0, 4, 8}; // 8 lines per window
        std::vector<float> left(num_windows * 8, 5.0f);
        std::vector<float> right(num_windows * 8, 2.0f);
        std::vector<uint8_t> ms_used(num_windows * num_swb, 0);

        // Enable M/S only on window 0 band 1, and window 3 band 0
        ms_used[0 * num_swb + 1] = 1;
        ms_used[3 * num_swb + 0] = 1;

        apply_ms_stereo_short(left.data(), right.data(), ms_used.data(), swb_offsets, num_swb, num_windows);

        // Window 0 band 0: unchanged (5, 2)
        for (int i = 0; i < 4; ++i) {
            assert(std::fabs(left[0 * 8 + i] - 5.0f) < 1e-5f);
            assert(std::fabs(right[0 * 8 + i] - 2.0f) < 1e-5f);
        }
        // Window 0 band 1: active -> L = 5+2=7, R = 5-2=3
        for (int i = 4; i < 8; ++i) {
            assert(std::fabs(left[0 * 8 + i] - 7.0f) < 1e-5f);
            assert(std::fabs(right[0 * 8 + i] - 3.0f) < 1e-5f);
        }
        // Window 3 band 0: active -> L = 7, R = 3
        for (int i = 0; i < 4; ++i) {
            assert(std::fabs(left[3 * 8 + i] - 7.0f) < 1e-5f);
            assert(std::fabs(right[3 * 8 + i] - 3.0f) < 1e-5f);
        }
        // Window 3 band 1: unchanged (5, 2)
        for (int i = 4; i < 8; ++i) {
            assert(std::fabs(left[3 * 8 + i] - 5.0f) < 1e-5f);
            assert(std::fabs(right[3 * 8 + i] - 2.0f) < 1e-5f);
        }
    }

    // -------------------------------------------------------------
    // Test 3: Intensity Stereo
    // -------------------------------------------------------------
    {
        std::cout << "Testing Intensity Stereo...\n";
        constexpr size_t num_swb = 4;
        int swb_offsets[num_swb + 1] = {0, 4, 8, 12, 16};
        float left[16]  = { 4.0f, 4.0f, 4.0f, 4.0f,
                            8.0f, 8.0f, 8.0f, 8.0f,
                            6.0f, 6.0f, 6.0f, 6.0f,
                            2.0f, 2.0f, 2.0f, 2.0f };
        float right[16] = { 1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f, 1.0f, 1.0f, 1.0f,
                            1.0f, 1.0f, 1.0f, 1.0f };

        // is_type: 0 = not IS, 1 = in-phase, 2 = out-of-phase (inverted)
        uint8_t is_type[num_swb] = {0, 1, 2, 15}; // 15 is INTENSITY_HCB (in-phase)
        int is_pos[num_swb] = {0, 4, 4, 0}; // 2^(-0.25 * 4) = 0.5, 2^(-0.25 * 0) = 1.0

        apply_intensity_stereo(left, right, is_pos, is_type, swb_offsets, num_swb);

        // Band 0: is_type 0 -> right unchanged = 1.0
        for (int i = 0; i < 4; ++i) {
            assert(std::fabs(right[i] - 1.0f) < 1e-5f);
        }
        // Band 1: is_type 1, is_pos 4 -> scale = +0.5 -> right = 8.0 * 0.5 = 4.0
        for (int i = 4; i < 8; ++i) {
            assert(std::fabs(right[i] - 4.0f) < 1e-5f);
        }
        // Band 2: is_type 2, is_pos 4 -> scale = -0.5 -> right = 6.0 * (-0.5) = -3.0
        for (int i = 8; i < 12; ++i) {
            assert(std::fabs(right[i] - (-3.0f)) < 1e-5f);
        }
        // Band 3: is_type 15, is_pos 0 -> scale = +1.0 -> right = 2.0 * 1.0 = 2.0
        for (int i = 12; i < 16; ++i) {
            assert(std::fabs(right[i] - 2.0f) < 1e-5f);
        }
    }

    // -------------------------------------------------------------
    // Test 4: PNS (Perceptual Noise Substitution)
    // -------------------------------------------------------------
    {
        std::cout << "Testing PNS...\n";
        constexpr size_t num_swb = 3;
        int swb_offsets[num_swb + 1] = {0, 16, 32, 48};
        float spec[48] = {0.0f};
        uint8_t pns_active[num_swb] = {1, 0, 1};
        int pns_energy[num_swb] = {100, 0, 108}; // 2^(0.25 * (100 - 100)) = 1.0, 2^(0.25 * (108 - 100)) = 4.0

        uint32_t rng_state = 123456789;
        apply_pns(spec, pns_active, pns_energy, swb_offsets, num_swb, rng_state);

        // Band 0: active with energy 100 -> total energy sum(spec^2) = (1.0)^2 = 1.0
        float band0_energy = 0.0f;
        for (int i = 0; i < 16; ++i) {
            band0_energy += spec[i] * spec[i];
        }
        assert(std::fabs(band0_energy - 1.0f) < 1e-4f);

        // Band 1: inactive -> all zeros
        for (int i = 16; i < 32; ++i) {
            assert(std::fabs(spec[i] - 0.0f) < 1e-6f);
        }

        // Band 2: active with energy 108 -> total energy sum(spec^2) = (4.0)^2 = 16.0
        float band2_energy = 0.0f;
        for (int i = 32; i < 48; ++i) {
            band2_energy += spec[i] * spec[i];
        }
        assert(std::fabs(band2_energy - 16.0f) < 1e-3f);
    }

    // -------------------------------------------------------------
    // Test 5: TNS Coefficient Decoding (PARCOR -> LPC)
    // -------------------------------------------------------------
    {
        std::cout << "Testing TNS Coefficient Decoding...\n";
        // 3-bit resolution (step = pi/8)
        // raw_coef = [2] -> gamma_0 = sin(2 * pi / 8) = sin(pi/4) = sqrt(2)/2 ~= 0.7071068
        int raw_coef[1] = {2};
        float lpc[1] = {0.0f};
        decode_tns_coef(1, 3, raw_coef, lpc);
        float expected_gamma0 = std::sin(2.0f * static_cast<float>(M_PI) / 8.0f);
        assert(std::fabs(lpc[0] - expected_gamma0) < 1e-5f);

        // 2nd order test
        int raw2[2] = {1, 2};
        float lpc2[2] = {0.0f};
        decode_tns_coef(2, 3, raw2, lpc2);
        float g0 = std::sin(1.0f * static_cast<float>(M_PI) / 8.0f);
        float g1 = std::sin(2.0f * static_cast<float>(M_PI) / 8.0f);
        float exp_a1 = g0 + g1 * g0;
        float exp_a2 = g1;
        assert(std::fabs(lpc2[0] - exp_a1) < 1e-5f);
        assert(std::fabs(lpc2[1] - exp_a2) < 1e-5f);
    }

    // -------------------------------------------------------------
    // Test 6: TNS Filter Application (All-pole IIR)
    // -------------------------------------------------------------
    {
        std::cout << "Testing TNS Filter Application (Upward & Downward)...\n";
        constexpr size_t num_swb = 4;
        int swb_offsets[num_swb + 1] = {0, 8, 16, 24, 32};
        float spec[32] = {0.0f};

        // Upward impulse response on band 1 (indices 8..15)
        // Input impulse at index 8 = 1.0f
        spec[8] = 1.0f;

        TnsData tns;
        tns.n_filt[0] = 1;
        tns.filters[0][0].start_band = 1;
        tns.filters[0][0].stop_band = 2; // band 1 (indices 8..15)
        tns.filters[0][0].order = 1;
        tns.filters[0][0].direction = 0; // upward
        tns.filters[0][0].coef[0] = 0.5f; // a_1 = 0.5

        apply_tns(spec, tns, swb_offsets, num_swb, WindowSequence::OnlyLong);

        // Before band 1 (0..7): unchanged = 0
        for (int i = 0; i < 8; ++i) {
            assert(std::fabs(spec[i] - 0.0f) < 1e-6f);
        }
        // Band 1 (8..15): y[0]=1.0, y[1]=-0.5, y[2]=0.25, y[3]=-0.125, y[4]=0.0625
        assert(std::fabs(spec[8] - 1.0f) < 1e-5f);
        assert(std::fabs(spec[9] - (-0.5f)) < 1e-5f);
        assert(std::fabs(spec[10] - 0.25f) < 1e-5f);
        assert(std::fabs(spec[11] - (-0.125f)) < 1e-5f);
        assert(std::fabs(spec[12] - 0.0625f) < 1e-5f);

        // Downward test on band 2 (indices 16..23)
        // Impulse at highest bin of band 2 (index 23) = 1.0f
        spec[23] = 1.0f;
        TnsData tns_down;
        tns_down.n_filt[0] = 1;
        tns_down.filters[0][0].start_band = 2;
        tns_down.filters[0][0].stop_band = 3; // band 2 (indices 16..23)
        tns_down.filters[0][0].order = 1;
        tns_down.filters[0][0].direction = 1; // downward
        tns_down.filters[0][0].coef[0] = 0.5f;

        apply_tns(spec, tns_down, swb_offsets, num_swb, WindowSequence::OnlyLong);

        assert(std::fabs(spec[23] - 1.0f) < 1e-5f);
        assert(std::fabs(spec[22] - (-0.5f)) < 1e-5f);
        assert(std::fabs(spec[21] - 0.25f) < 1e-5f);
        assert(std::fabs(spec[20] - (-0.125f)) < 1e-5f);
    }

    std::cout << "All AAC Stereo & TNS tests passed!\n";
    return 0;
}
