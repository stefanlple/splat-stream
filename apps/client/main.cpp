#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "net/client.h"
#include "render/splat_renderer.h"

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 5555;
    bool render = true;
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--no-render") == 0) render = false;
        else if (positional++ == 0) host = argv[i];
        else port = static_cast<uint16_t>(std::stoi(argv[i]));
    }

    try {
        Client client;
        client.connect(host, port);
        std::cout << "connected as client " << client.id() << ", downloading scene\n";

        auto nextPrint = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (client.connected() && !client.sceneComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (std::chrono::steady_clock::now() >= nextPrint) {
                size_t done, total, bytes;
                client.sceneProgress(done, total, bytes);
                std::cout << "  " << done << "/" << total << " chunks\n";
                nextPrint += std::chrono::seconds(1);
            }
        }
        if (!client.sceneComplete()) {
            std::cerr << "error: " << client.error() << "\n";
            return 1;
        }

        size_t done, total, bytes;
        client.sceneProgress(done, total, bytes);
        double ms = client.sceneTransferMs();
        GaussianScene scene = client.takeScene();
        std::cout << "received " << scene.count() << " gaussians (" << bytes / 1e6 << " MB compressed) in " << ms
                  << " ms, " << bytes / 1e6 / (ms / 1000.0) << " MB/s\n";

        RttStats rtt = client.takeRttStats();
        if (rtt.count > 0) {
            std::cout << "rtt during download: min " << rtt.minMs << " ms, avg " << rtt.avgMs << " ms, max "
                      << rtt.maxMs << " ms\n";
        }

        if (render) {
            SplatRenderer renderer(scene, 1280, 720);
            renderer.run();
        }
        client.disconnect();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
