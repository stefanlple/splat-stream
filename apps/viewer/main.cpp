#include <chrono>
#include <iostream>

#include "render/splat_renderer.h"
#include "scene/ply_loader.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: viewer <scene.ply>\n";
        return 1;
    }

    try {
        auto start = std::chrono::steady_clock::now();
        GaussianScene scene = loadPly(argv[1]);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::cout << "loaded " << scene.count() << " gaussians in " << ms.count() << " ms\n";

        SplatRenderer renderer(scene, 1280, 720);
        renderer.run();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
