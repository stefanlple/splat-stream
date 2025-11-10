#include "client.h"

#include <algorithm>
#include <stdexcept>

#include "protocol/framing.h"

using asio::ip::tcp;
using asio::ip::udp;

Client::Client() : tcpSocket(io), udpSocket(io), pingTimer(io) {}

Client::~Client() { disconnect(); }

void Client::connect(const std::string& host, uint16_t port) {
    tcp::resolver resolver(io);
    asio::connect(tcpSocket, resolver.resolve(host, std::to_string(port)));
    tcpSocket.set_option(tcp::no_delay(true));

    splatstream::Message hello;
    hello.mutable_hello()->set_version(kProtocolVersion);
    asio::write(tcpSocket, asio::buffer(encodeMessage(hello)));

    asio::read(tcpSocket, asio::buffer(header));
    uint32_t size = decodeLength(header);
    if (size == 0) throw std::runtime_error("bad reply from server");
    body.resize(size);
    asio::read(tcpSocket, asio::buffer(body));

    splatstream::Message reply;
    if (!decodeMessage(body.data(), body.size(), reply) || !reply.has_welcome()) {
        throw std::runtime_error("server did not send a welcome");
    }
    clientId = reply.welcome().client_id();

    serverUdp = udp::endpoint(tcpSocket.remote_endpoint().address(), static_cast<uint16_t>(reply.welcome().udp_port()));
    udpSocket.open(udp::v4());

    isConnected = true;
    readHeader();
    schedulePing();
    thread = std::thread([this] { io.run(); });
}

void Client::disconnect() {
    if (thread.joinable()) {
        asio::post(io, [this] {
            asio::error_code ec;
            tcpSocket.close(ec);
            pingTimer.cancel();
        });
        thread.join();
    }
    isConnected = false;
}

void Client::sendCamera(const float position[3], const float rotation[4]) {
    if (!isConnected) return;
    splatstream::CameraUpdate update;
    update.set_client_id(clientId);
    update.set_sequence(++cameraSequence);
    update.set_x(position[0]);
    update.set_y(position[1]);
    update.set_z(position[2]);
    update.set_qw(rotation[0]);
    update.set_qx(rotation[1]);
    update.set_qy(rotation[2]);
    update.set_qz(rotation[3]);

    asio::error_code ec;
    udpSocket.send_to(asio::buffer(update.SerializeAsString()), serverUdp, 0, ec);
}

RttStats Client::takeRttStats() {
    std::lock_guard lock(rttMutex);
    RttStats stats;
    stats.count = rtts.size();
    if (!rtts.empty()) {
        stats.minMs = *std::min_element(rtts.begin(), rtts.end());
        stats.maxMs = *std::max_element(rtts.begin(), rtts.end());
        double sum = 0;
        for (double r : rtts) sum += r;
        stats.avgMs = sum / rtts.size();
    }
    rtts.clear();
    return stats;
}

void Client::readHeader() {
    asio::async_read(tcpSocket, asio::buffer(header), [this](asio::error_code ec, size_t) {
        uint32_t size = ec ? 0 : decodeLength(header);
        if (size == 0) {
            isConnected = false;
            pingTimer.cancel();
            return;
        }
        readBody(size);
    });
}

void Client::readBody(uint32_t size) {
    body.resize(size);
    asio::async_read(tcpSocket, asio::buffer(body), [this](asio::error_code ec, size_t) {
        splatstream::Message message;
        if (ec || !decodeMessage(body.data(), body.size(), message)) {
            isConnected = false;
            pingTimer.cancel();
            return;
        }
        if (message.has_pong()) {
            double ms = (nowMicros() - message.pong().client_time_us()) / 1000.0;
            std::lock_guard lock(rttMutex);
            rtts.push_back(ms);
        }
        readHeader();
    });
}

void Client::schedulePing() {
    splatstream::Message ping;
    ping.mutable_ping()->set_id(nextPingId++);
    ping.mutable_ping()->set_client_time_us(nowMicros());
    outbox.push_back(encodeMessage(ping));
    if (outbox.size() == 1) writeNext();

    pingTimer.expires_after(std::chrono::seconds(1));
    pingTimer.async_wait([this](asio::error_code ec) {
        if (!ec) schedulePing();
    });
}

void Client::writeNext() {
    asio::async_write(tcpSocket, asio::buffer(outbox.front()), [this](asio::error_code ec, size_t) {
        if (ec) return;
        outbox.pop_front();
        if (!outbox.empty()) writeNext();
    });
}
