#pragma once

#include <cstdint>
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

// renders without a window, for the server
class OffscreenRenderer {
public:
    // scene only has to stay alive during the constructor
    OffscreenRenderer(const GaussianScene& scene, int width, int height);
    ~OffscreenRenderer();

    // returns width * height BGRA pixels, valid until the next call.
    // rotation is a (w, x, y, z) quaternion
    const uint8_t* render(const float position[3], const float rotation[4]);

    int width() const { return frameWidth; }
    int height() const { return frameHeight; }

private:
    std::unique_ptr<VulkanSplatting> splatting;
    const uint8_t* frame = nullptr;
    int frameWidth;
    int frameHeight;
};
