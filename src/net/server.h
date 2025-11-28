#pragma once

#include <asio.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "protocol/scene_stream.h"

struct CameraState {
    uint32_t sequence = 0;
    uint64_t updates = 0;
    float position[3] = {0, 0, 0};
    float rotation[4] = {1, 0, 0, 0};
};

class Session;

class Server {
public:
    Server(asio::io_context& io, uint16_t tcpPort, uint16_t udpPort,
           std::shared_ptr<const PreparedScene> scene = nullptr);

    uint16_t tcpPort() const;
    uint16_t udpPort() const;
    size_t clientCount();
    std::vector<std::pair<uint32_t, CameraState>> cameras();
    void stop();

private:
    friend class Session;

    void acceptNext();
    void receiveCamera();
    void removeSession(uint32_t id);

    std::shared_ptr<const PreparedScene> scene;
    asio::io_context& io;
    asio::ip::tcp::acceptor acceptor;
    asio::ip::udp::socket udpSocket;
    asio::ip::udp::endpoint udpSender;
    char udpBuffer[512];

    std::mutex mutex;
    std::map<uint32_t, std::shared_ptr<Session>> sessions;
    uint32_t nextId = 1;
};
