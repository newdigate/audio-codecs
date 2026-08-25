#include "src/vorbis/vorbis_floor.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace audio_codecs::vorbis;

    VorbisFloor1Config cfg;
    cfg.partitions = 1;
    cfg.partition_class[0] = 0;
    cfg.class_dimensions[0] = 1;
    cfg.class_subclasses[0] = 0;
    cfg.class_masterbooks[0] = 0;
    cfg.multiplier = 1;
    cfg.rangebits = 8;
    cfg.post_list = {0, 256, 128}; // 0, end, middle
    vorbis_floor1_setup_neighbors(cfg);

    assert(cfg.low_neighbors.size() == 3);
    assert(cfg.high_neighbors.size() == 3);
    // Point 2 (x=128): low neighbor is index 0 (x=0), high neighbor is index 1 (x=256)
    assert(cfg.low_neighbors[2] == 0);
    assert(cfg.high_neighbors[2] == 1);

    int32_t y_vals[3] = {100, 100, 50};
    std::vector<float> floor_curve(256);
    vorbis_floor1_render(cfg, y_vals, floor_curve.data(), 256);

    assert(floor_curve[0] > 0.0f);
    assert(floor_curve[255] > 0.0f);

    // Test fitting target spectrum
    std::vector<float> target_spec(256, 0.1f);
    int32_t fitted_y[3] = {0};
    vorbis_floor1_fit(target_spec.data(), 256, cfg, fitted_y);
    assert(fitted_y[0] > 0);
    assert(fitted_y[1] > 0);

    std::cout << "Vorbis Floor 1 rendering tests passed!\n";
    return 0;
}
