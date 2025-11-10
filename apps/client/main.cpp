#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "net/client.h"

int main(int argc, char** argv) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(std::stoi(argv[2])) : 5555;
    int seconds = argc > 3 ? std::stoi(argv[3]) : 10;

    try {
        Client client;
        client.connect(host, port);
        std::cout << "connected as client " << client.id() << "\n";

        // fake camera that circles around the origin, sent 60 times a second
        auto start = std::chrono::steady_clock::now();
        auto nextPrint = start + std::chrono::seconds(1);
        auto nextFrame = start;
        while (client.connected() && std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
            float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
            float position[3] = {3.0f * std::cos(t), 0.0f, 3.0f * std::sin(t)};
            float rotation[4] = {std::cos(t / 2), 0.0f, std::sin(t / 2), 0.0f};
            client.sendCamera(position, rotation);

            if (std::chrono::steady_clock::now() >= nextPrint) {
                RttStats rtt = client.takeRttStats();
                if (rtt.count > 0) {
                    std::cout << "rtt min " << rtt.minMs << " ms, avg " << rtt.avgMs << " ms, max " << rtt.maxMs << " ms\n";
                }
                nextPrint += std::chrono::seconds(1);
            }
            nextFrame += std::chrono::microseconds(1000000 / 60);
            std::this_thread::sleep_until(nextFrame);
        }

        if (!client.connected()) std::cout << "lost connection to server\n";
        client.disconnect();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
