#include "splat_renderer.h"

#include <cmath>

#include "3dgs.h"

void packVertices(const GaussianScene& scene, float* out) {
    size_t restPerChannel = scene.shRest.size() / scene.count() / 3;

    for (size_t i = 0; i < scene.count(); i++) {
        float* v = out + i * 60;

        v[0] = scene.positions[i * 3 + 0];
        v[1] = scene.positions[i * 3 + 1];
        v[2] = scene.positions[i * 3 + 2];
        v[3] = 1.0f;

        v[4] = std::exp(scene.scales[i * 3 + 0]);
        v[5] = std::exp(scene.scales[i * 3 + 1]);
        v[6] = std::exp(scene.scales[i * 3 + 2]);
        v[7] = 1.0f / (1.0f + std::exp(-scene.opacities[i]));

        const float* q = &scene.rotations[i * 4];
        float length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (int k = 0; k < 4; k++) {
            v[8 + k] = q[k] / length;
        }

        // the ply stores SH as all red, then all green, then all blue.
        // the renderer wants rgb interleaved per coefficient, always 16 coefficients.
        float* sh = v + 12;
        sh[0] = scene.shDC[i * 3 + 0];
        sh[1] = scene.shDC[i * 3 + 1];
        sh[2] = scene.shDC[i * 3 + 2];
        const float* rest = scene.shRest.data() + i * restPerChannel * 3;
        for (size_t j = 1; j < 16; j++) {
            for (size_t c = 0; c < 3; c++) {
                sh[j * 3 + c] = j - 1 < restPerChannel ? rest[c * restPerChannel + j - 1] : 0.0f;
            }
        }
    }
}

SplatRenderer::SplatRenderer(const GaussianScene& scene, int width, int height) {
    VulkanSplatting::RendererConfiguration config;
    config.enableGui = true;
    config.window = VulkanSplatting::createGlfwWindow("SplatStream", width, height);
    config.numVertices = static_cast<uint32_t>(scene.count());
    config.fillVertices = [&scene](float* out) { packVertices(scene, out); };
    splatting = std::make_unique<VulkanSplatting>(config);
}

SplatRenderer::~SplatRenderer() = default;

void SplatRenderer::run() {
    splatting->start();
}
