#include "framing.h"

#include <chrono>
#include <cmath>

std::string encodeMessage(const splatstream::Message& message) {
    std::string body = message.SerializeAsString();
    uint32_t size = static_cast<uint32_t>(body.size());

    std::string frame(4, '\0');
    frame[0] = static_cast<char>((size >> 24) & 0xff);
    frame[1] = static_cast<char>((size >> 16) & 0xff);
    frame[2] = static_cast<char>((size >> 8) & 0xff);
    frame[3] = static_cast<char>(size & 0xff);
    return frame + body;
}

uint32_t decodeLength(const uint8_t header[4]) {
    uint32_t size = (uint32_t(header[0]) << 24) | (uint32_t(header[1]) << 16) |
                    (uint32_t(header[2]) << 8) | uint32_t(header[3]);
    if (size == 0 || size > kMaxMessageSize) return 0;
    return size;
}

bool decodeMessage(const char* data, size_t size, splatstream::Message& out) {
    if (!out.ParseFromArray(data, static_cast<int>(size))) return false;
    return out.payload_case() != splatstream::Message::PAYLOAD_NOT_SET;
}

bool decodeCamera(const char* data, size_t size, splatstream::CameraUpdate& out) {
    if (!out.ParseFromArray(data, static_cast<int>(size))) return false;
    float values[] = {out.x(), out.y(), out.z(), out.qw(), out.qx(), out.qy(), out.qz()};
    for (float v : values) {
        if (!std::isfinite(v)) return false;
    }
    return out.client_id() != 0;
}

uint64_t nowMicros() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}
