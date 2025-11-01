#pragma once

#include <memory>

#include "scene/gaussian_scene.h"

class VulkanSplatting;

// Packs one gaussian per 60 floats in the layout 3DGS.cpp expects.
void packVertices(const GaussianScene& scene, float* out);

class SplatRenderer {
public:
    // scene has to stay alive until run() returns
    SplatRenderer(const GaussianScene& scene, int width, int height);
    ~SplatRenderer();

    void run();

private:
    std::unique_ptr<VulkanSplatting> splatting;
};
