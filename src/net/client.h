#pragma once

#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct RttStats {
    size_t count = 0;
    double minMs = 0;
    double avgMs = 0;
    double maxMs = 0;
};

class Client {
public:
    Client();
    ~Client();

    // blocks until the server sent its welcome, throws on failure
    void connect(const std::string& host, uint16_t port);
    void disconnect();

    bool connected() const { return isConnected; }
    uint32_t id() const { return clientId; }

    // call from one thread only
    void sendCamera(const float position[3], const float rotation[4]);

    // rtt measurements since the last call
    RttStats takeRttStats();

private:
    void readHeader();
    void readBody(uint32_t size);
    void schedulePing();
    void writeNext();

    asio::io_context io;
    asio::ip::tcp::socket tcpSocket;
    asio::ip::udp::socket udpSocket;
    asio::ip::udp::endpoint serverUdp;
    asio::steady_timer pingTimer;
    std::thread thread;

    uint32_t clientId = 0;
    uint32_t cameraSequence = 0;
    uint64_t nextPingId = 1;
    std::atomic<bool> isConnected{false};

    uint8_t header[4];
    std::vector<char> body;
    std::deque<std::string> outbox;

    std::mutex rttMutex;
    std::vector<double> rtts;
};
