#pragma once

#include <cstddef>
#include <vector>

// Values are stored as they are in the .ply file (log scales, opacity before
// sigmoid, unnormalized wxyz quaternions). The renderer converts them.
struct GaussianScene {
    int shDegree = 0;
    std::vector<float> positions;
    std::vector<float> scales;
    std::vector<float> rotations;
    std::vector<float> opacities;
    std::vector<float> shDC;
    std::vector<float> shRest;

    size_t count() const { return opacities.size(); }
};
