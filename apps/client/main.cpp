#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <thread>
#include <vector>

#include "net/client.h"
#include "protocol/framing.h"
#include "render/splat_renderer.h"
#include "video/video_decoder.h"

using Clock = std::chrono::steady_clock;

int downloadAndRender(Client& client, bool render) {
    auto nextPrint = Clock::now() + std::chrono::seconds(1);
    while (client.connected() && !client.sceneComplete()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (Clock::now() >= nextPrint) {
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

    if (render) {
        SplatRenderer renderer(scene, 1280, 720);
        renderer.run();
    }
    return 0;
}

struct VideoStats {
    int frames = 0;
    int latencySamples = 0;
    size_t bytes = 0;
    double latencySum = 0, latencyMax = 0, latencyMin = 1e9;
    double renderSum = 0, encodeSum = 0, decodeSum = 0;
};

// server renders, we decode and show the video and send our camera back
int streamVideo(Client& client, bool spin) {
    if (!glfwInit()) {
        std::cerr << "error: could not init glfw\n";
        return 1;
    }
    GLFWwindow* window = glfwCreateWindow(1280, 720, "SplatStream (video)", nullptr, nullptr);
    if (!window) {
        std::cerr << "error: could not create window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // no vsync, it would add up to a frame of latency

    // same controls as the local viewer: wasd, space/shift, drag with the left mouse button to look
    glm::vec3 position(0.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    double lastX, lastY;
    glfwGetCursorPos(window, &lastX, &lastY);

    VideoDecoder decoder;
    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    VideoStats stats;
    auto lastTick = Clock::now();
    auto nextCamera = lastTick;
    auto nextPrint = lastTick + std::chrono::seconds(1);

    while (!glfwWindowShouldClose(window) && client.connected()) {
        glfwPollEvents();
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        lastTick = now;

        double x, y;
        glfwGetCursorPos(window, &x, &y);
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            rotation = glm::rotate(rotation, float(x - lastX) * 0.005f, glm::vec3(0, -1, 0));
            rotation = glm::rotate(rotation, float(y - lastY) * 0.005f, glm::vec3(-1, 0, 0));
        }
        lastX = x;
        lastY = y;
        if (spin) rotation = glm::rotate(rotation, 0.8f * dt, glm::vec3(0, 1, 0));  // for measuring with motion

        glm::vec3 direction(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) direction.z -= 1;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) direction.z += 1;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) direction.x -= 1;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) direction.x += 1;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) direction.y += 1;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) direction.y -= 1;
        if (direction != glm::vec3(0.0f)) {
            position += glm::mat3_cast(rotation) * glm::normalize(direction) * 2.0f * dt;
        }

        if (now >= nextCamera) {
            float p[3] = {position.x, position.y, position.z};
            float q[4] = {rotation.w, rotation.x, rotation.y, rotation.z};
            client.sendCamera(p, q);
            nextCamera = now + std::chrono::microseconds(1000000 / 120);
        }

        // decode everything that arrived (frames depend on each other), show the newest
        std::vector<splatstream::VideoFrame> shown;
        splatstream::VideoFrame frame;
        while (client.takeVideoFrame(frame)) {
            auto d0 = Clock::now();
            if (decoder.decode(frame.data(), rgba, width, height)) {
                stats.decodeSum += std::chrono::duration<double, std::milli>(Clock::now() - d0).count();
                stats.bytes += frame.data().size();
                shown.push_back(std::move(frame));
            }
        }

        glClear(GL_COLOR_BUFFER_BIT);
        if (width > 0) {
            int fbWidth, fbHeight;
            glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
            glRasterPos2f(-1, 1);
            glPixelZoom(float(fbWidth) / width, -float(fbHeight) / height);
            glDrawPixels(width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
        glfwSwapBuffers(window);

        // camera input -> frame on screen, all measured with our own clock
        uint64_t displayed = nowMicros();
        for (auto& f : shown) {
            stats.frames++;
            stats.renderSum += f.render_us() / 1000.0;
            stats.encodeSum += f.encode_us() / 1000.0;
            if (f.camera_time_us() == 0) continue;
            double latency = (displayed - f.camera_time_us()) / 1000.0;
            stats.latencySamples++;
            stats.latencySum += latency;
            stats.latencyMax = std::max(stats.latencyMax, latency);
            stats.latencyMin = std::min(stats.latencyMin, latency);
        }

        if (Clock::now() >= nextPrint) {
            if (stats.frames > 0 && stats.latencySamples > 0) {
                std::cout << stats.frames << " fps, " << stats.bytes * 8 / 1e6 << " Mbit/s, latency avg "
                          << stats.latencySum / stats.latencySamples << " ms (min " << stats.latencyMin << ", max "
                          << stats.latencyMax << "), server render " << stats.renderSum / stats.frames
                          << " ms, encode " << stats.encodeSum / stats.frames << " ms, decode "
                          << stats.decodeSum / stats.frames << " ms" << std::endl;
            }
            stats = VideoStats();
            nextPrint += std::chrono::seconds(1);
        }
    }

    if (!client.connected()) std::cerr << "error: " << client.error() << "\n";
    glfwDestroyWindow(window);
    glfwTerminate();
    return client.connected() ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 5555;
    bool render = true;
    bool video = false;
    bool spin = false;
    int positional = 0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--no-render") == 0) render = false;
        else if (std::strcmp(argv[i], "--video") == 0) video = true;
        else if (std::strcmp(argv[i], "--spin") == 0) spin = true;
        else if (positional++ == 0) host = argv[i];
        else port = static_cast<uint16_t>(std::stoi(argv[i]));
    }

    try {
        Client client;
        client.connect(host, port, video);
        std::cout << "connected as client " << client.id() << (video ? ", streaming video\n" : ", downloading scene\n");
        int result = video ? streamVideo(client, spin) : downloadAndRender(client, render);
        client.disconnect();
        return result;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
