#include <asio.hpp>
#include <atomic>
#include <memory>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "net/server.h"
#include "protocol/framing.h"
#include "render/splat_renderer.h"
#include "scene/ply_loader.h"
#include "video/video_encoder.h"

using Clock = std::chrono::steady_clock;

uint32_t microsBetween(Clock::time_point a, Clock::time_point b) {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(b - a).count());
}

// renders and encodes frames for the video client, on its own thread
void videoLoop(Server& server, const GaussianScene& scene, std::atomic<bool>& running) {
    const int width = 1280, height = 720, fps = 60, bitrateKbps = 8000;
    OffscreenRenderer renderer(scene, width, height);
    std::cout << "video renderer ready (" << width << "x" << height << ")\n";

    std::unique_ptr<VideoEncoder> encoder;
    uint32_t currentClient = 0;
    uint32_t frameId = 0;
    auto nextFrame = Clock::now();

    while (running) {
        uint32_t id;
        CameraState camera;
        if (!server.videoClient(id, camera)) {
            encoder.reset();
            currentClient = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (id != currentClient) {
            // a new encoder makes sure the new client starts with a keyframe
            encoder = std::make_unique<VideoEncoder>(width, height, fps, bitrateKbps);
            currentClient = id;
            frameId = 0;
        }
        // frames waiting in the socket only add latency, so skip rendering until it drains
        if (server.queuedFrames(id) > 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        auto t0 = Clock::now();
        const uint8_t* pixels = renderer.render(camera.position, camera.rotation);
        auto t1 = Clock::now();
        std::string data = encoder->encode(pixels);
        auto t2 = Clock::now();

        splatstream::Message message;
        auto* frame = message.mutable_video_frame();
        frame->set_frame_id(frameId++);
        frame->set_camera_sequence(camera.sequence);
        frame->set_camera_time_us(camera.clientTimeUs);
        frame->set_render_us(microsBetween(t0, t1));
        frame->set_encode_us(microsBetween(t1, t2));
        frame->set_data(std::move(data));
        server.sendFrame(id, std::make_shared<const std::string>(encodeMessage(message)));

        nextFrame += std::chrono::microseconds(1000000 / fps);
        if (nextFrame < Clock::now()) nextFrame = Clock::now();
        std::this_thread::sleep_until(nextFrame);
    }
}

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

        std::atomic<bool> running{true};
        std::thread video([&] {
            try {
                videoLoop(server, scene, running);
            } catch (const std::exception& e) {
                std::cerr << "video disabled: " << e.what() << "\n";
            }
        });

        asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](asio::error_code, int) {
            std::cout << "shutting down\n";
            running = false;
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
        running = false;
        video.join();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
