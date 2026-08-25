#include "src/vorbis/vorbis_residue.h"
#include "src/vorbis/vorbis_mapping.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;

    // 1. Test polar channel decoupling
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
    assert(std::fabs(left[0] - 10.0f) < 1e-4f);
    assert(std::fabs(right[0] - 6.0f) < 1e-4f);

    // 2. Test roundtrip coupling -> decoupling
    float orig_l[4] = {12.0f, -8.0f, 3.5f, -1.0f};
    float orig_r[4] = {4.0f, -2.0f, 9.0f, -6.5f};
    float test_l[4], test_r[4];
    std::memcpy(test_l, orig_l, sizeof(orig_l));
    std::memcpy(test_r, orig_r, sizeof(orig_r));
    float* test_ch[2] = {test_l, test_r};

    vorbis_mapping_couple(map, test_ch, 4);
    vorbis_mapping_decouple(map, test_ch, 4);

    for (int i = 0; i < 4; ++i) {
        assert(std::fabs(test_l[i] - orig_l[i]) < 1e-4f);
        assert(std::fabs(test_r[i] - orig_r[i]) < 1e-4f);
    }

    std::cout << "Vorbis Residue and Channel Coupling tests passed!\n";
    return 0;
}
