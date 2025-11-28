#include "server.h"

#include <deque>
#include <iostream>

#include "protocol/framing.h"

using asio::ip::tcp;
using asio::ip::udp;

const auto kIdleTimeout = std::chrono::seconds(5);

// All handlers of a session run on the socket's strand, so a session's own
// members don't need locking. Only `camera` is also touched by the udp handler.
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(Server& server, tcp::socket socket, uint32_t id)
        : id(id), server(server), socket(std::move(socket)), timer(this->socket.get_executor()) {
        asio::error_code ec;
        address = this->socket.remote_endpoint(ec).address();
    }

    void start() {
        resetTimeout();
        readHeader();
    }

    void close() {
        if (closed) return;
        closed = true;
        asio::error_code ec;
        socket.close(ec);
        timer.cancel();
        server.removeSession(id);
    }

    void closeLater() {
        asio::post(socket.get_executor(), [self = shared_from_this()] { self->close(); });
    }

    const uint32_t id;
    asio::ip::address address;
    std::mutex cameraMutex;
    CameraState camera;

private:
    void resetTimeout() {
        timer.expires_after(kIdleTimeout);
        timer.async_wait([self = shared_from_this()](asio::error_code ec) {
            if (!ec) {
                std::cout << "client " << self->id << " timed out\n";
                self->close();
            }
        });
    }

    void readHeader() {
        asio::async_read(socket, asio::buffer(header), [self = shared_from_this()](asio::error_code ec, size_t) {
            if (ec) return self->close();
            uint32_t size = decodeLength(self->header);
            if (size == 0) {
                std::cout << "client " << self->id << " sent a bad length, disconnecting\n";
                return self->close();
            }
            self->readBody(size);
        });
    }

    void readBody(uint32_t size) {
        body.resize(size);
        asio::async_read(socket, asio::buffer(body), [self = shared_from_this()](asio::error_code ec, size_t) {
            if (ec) return self->close();
            splatstream::Message message;
            if (!decodeMessage(self->body.data(), self->body.size(), message)) {
                std::cout << "client " << self->id << " sent a malformed message, disconnecting\n";
                return self->close();
            }
            self->resetTimeout();
            if (self->handle(message)) self->readHeader();
        });
    }

    bool handle(const splatstream::Message& message) {
        if (!greeted) {
            if (!message.has_hello() || message.hello().version() != kProtocolVersion) {
                std::cout << "client " << id << " did not send a valid hello, disconnecting\n";
                close();
                return false;
            }
            greeted = true;
            splatstream::Message reply;
            reply.mutable_welcome()->set_client_id(id);
            reply.mutable_welcome()->set_udp_port(server.udpPort());
            send(reply);
            std::cout << "client " << id << " connected from " << address.to_string() << "\n";

            if (server.scene) {
                sendFrame(server.scene->info);
                for (auto& chunk : server.scene->chunks) sendFrame(chunk);
            }
            return true;
        }

        if (message.has_ping()) {
            splatstream::Message reply;
            reply.mutable_pong()->set_id(message.ping().id());
            reply.mutable_pong()->set_client_time_us(message.ping().client_time_us());
            send(reply);
        }
        return true;
    }

    void send(const splatstream::Message& message) {
        sendFrame(std::make_shared<const std::string>(encodeMessage(message)));
    }

    // frames are shared, so sending the scene to many clients doesn't copy it
    void sendFrame(std::shared_ptr<const std::string> frame) {
        outbox.push_back(std::move(frame));
        if (outbox.size() == 1) writeNext();
    }

    void writeNext() {
        asio::async_write(socket, asio::buffer(*outbox.front()), [self = shared_from_this()](asio::error_code ec, size_t) {
            if (ec) return self->close();
            self->outbox.pop_front();
            if (!self->outbox.empty()) self->writeNext();
        });
    }

    Server& server;
    tcp::socket socket;
    asio::steady_timer timer;
    uint8_t header[4];
    std::vector<char> body;
    std::deque<std::shared_ptr<const std::string>> outbox;
    bool greeted = false;
    bool closed = false;
};

Server::Server(asio::io_context& io, uint16_t tcpPort, uint16_t udpPort, std::shared_ptr<const PreparedScene> scene)
    : scene(std::move(scene)),
      io(io),
      acceptor(io, tcp::endpoint(tcp::v4(), tcpPort)),
      udpSocket(io, udp::endpoint(udp::v4(), udpPort)) {
    acceptNext();
    receiveCamera();
}

uint16_t Server::tcpPort() const { return acceptor.local_endpoint().port(); }
uint16_t Server::udpPort() const { return udpSocket.local_endpoint().port(); }

size_t Server::clientCount() {
    std::lock_guard lock(mutex);
    return sessions.size();
}

std::vector<std::pair<uint32_t, CameraState>> Server::cameras() {
    std::lock_guard lock(mutex);
    std::vector<std::pair<uint32_t, CameraState>> result;
    for (auto& [id, session] : sessions) {
        std::lock_guard cameraLock(session->cameraMutex);
        result.push_back({id, session->camera});
    }
    return result;
}

void Server::stop() {
    asio::post(io, [this] {
        asio::error_code ec;
        acceptor.close(ec);
        udpSocket.close(ec);
    });
    std::lock_guard lock(mutex);
    for (auto& [id, session] : sessions) session->closeLater();
}

void Server::acceptNext() {
    acceptor.async_accept(asio::make_strand(io), [this](asio::error_code ec, tcp::socket socket) {
        if (ec) return;  // acceptor was closed
        std::shared_ptr<Session> session;
        {
            std::lock_guard lock(mutex);
            uint32_t id = nextId++;
            session = std::make_shared<Session>(*this, std::move(socket), id);
            sessions[id] = session;
        }
        session->start();
        acceptNext();
    });
}

void Server::receiveCamera() {
    udpSocket.async_receive_from(asio::buffer(udpBuffer), udpSender, [this](asio::error_code ec, size_t size) {
        if (ec == asio::error::operation_aborted) return;
        splatstream::CameraUpdate update;
        if (!ec && decodeCamera(udpBuffer, size, update)) {
            std::lock_guard lock(mutex);
            auto it = sessions.find(update.client_id());
            // only accept updates from the same machine as the tcp connection, and drop old packets
            if (it != sessions.end() && it->second->address == udpSender.address()) {
                std::lock_guard cameraLock(it->second->cameraMutex);
                CameraState& camera = it->second->camera;
                if (update.sequence() > camera.sequence) {
                    camera.sequence = update.sequence();
                    camera.updates++;
                    camera.position[0] = update.x();
                    camera.position[1] = update.y();
                    camera.position[2] = update.z();
                    camera.rotation[0] = update.qw();
                    camera.rotation[1] = update.qx();
                    camera.rotation[2] = update.qy();
                    camera.rotation[3] = update.qz();
                }
            }
        }
        receiveCamera();
    });
}

void Server::removeSession(uint32_t id) {
    std::lock_guard lock(mutex);
    if (sessions.erase(id)) std::cout << "client " << id << " disconnected\n";
}
