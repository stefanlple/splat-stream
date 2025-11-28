#include <asio.hpp>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "net/server.h"
#include "scene/ply_loader.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: server <scene.ply> [tcpPort] [udpPort]\n";
        return 1;
    }
    uint16_t tcpPort = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 5555;
    uint16_t udpPort = argc > 3 ? static_cast<uint16_t>(std::stoi(argv[3])) : 5556;

    try {
        auto start = std::chrono::steady_clock::now();
        GaussianScene scene = loadPly(argv[1]);
        auto loaded = std::chrono::steady_clock::now();
        auto prepared = prepareScene(scene);
        auto done = std::chrono::steady_clock::now();
        auto ms = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
        std::cout << "loaded " << scene.count() << " gaussians in " << ms(start, loaded) << " ms, compressed "
                  << prepared->chunks.size() << " chunks in " << ms(loaded, done) << " ms: "
                  << prepared->rawBytes / 1e6 << " MB -> " << prepared->compressedBytes / 1e6 << " MB\n";

        asio::io_context io;
        Server server(io, tcpPort, udpPort, prepared);
        std::cout << "listening on tcp " << server.tcpPort() << ", udp " << server.udpPort() << "\n";

        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](asio::error_code, int) {
            std::cout << "shutting down\n";
            server.stop();
            io.stop();
        });

        asio::steady_timer statsTimer(io);
        std::map<uint32_t, uint64_t> lastUpdates;
        std::function<void()> printStats = [&] {
            statsTimer.expires_after(std::chrono::seconds(1));
            statsTimer.async_wait([&](asio::error_code ec) {
                if (ec) return;
                for (auto& [id, camera] : server.cameras()) {
                    uint64_t perSecond = camera.updates - lastUpdates[id];
                    lastUpdates[id] = camera.updates;
                    if (perSecond == 0) continue;
                    std::cout << "client " << id << ": " << perSecond << " camera updates/s, pos ("
                              << camera.position[0] << ", " << camera.position[1] << ", " << camera.position[2] << ")\n";
                }
                printStats();
            });
        };
        printStats();

        unsigned threadCount = std::max(2u, std::thread::hardware_concurrency());
        std::vector<std::thread> threads;
        for (unsigned i = 1; i < threadCount; i++) {
            threads.emplace_back([&io] { io.run(); });
        }
        io.run();
        for (auto& t : threads) t.join();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
