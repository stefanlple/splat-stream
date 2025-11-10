#include <asio.hpp>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "net/server.h"

int main(int argc, char** argv) {
    uint16_t tcpPort = argc > 1 ? static_cast<uint16_t>(std::stoi(argv[1])) : 5555;
    uint16_t udpPort = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 5556;

    try {
        asio::io_context io;
        Server server(io, tcpPort, udpPort);
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
                    std::cout << "client " << id << ": " << camera.updates - lastUpdates[id] << " camera updates/s, pos ("
                              << camera.position[0] << ", " << camera.position[1] << ", " << camera.position[2] << ")\n";
                    lastUpdates[id] = camera.updates;
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
